#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Post-processes the extracted `compile_commands.json`. Run by ./compile_commands-update.sh.

Two passes, both fixing a way the extracted database does not match what clang-tidy / clangd are
actually asked about:

1. LOCAL-MODULE SOURCE PATHS (every platform). The composable extras are separate bazel modules
   pulled in with `local_path_override`, so the extractor records their sources under the execroot
   spelling `external/xff_archive+/archive_reader.cc`. Nothing ever asks about THAT path: an editor
   opens `extra_modules/archive/archive_reader.cc`, and a lint run names the same repo-relative path.
   A compile database is looked up BY PATH, so without this pass every extras file has no entry of
   its own and tooling falls back to an unrelated command that cannot even resolve the extra's own
   header (`'xff/archive/archive_reader.h' file not found`), making its findings noise. The external
   directory is a symlink to the source directory, so pointing the entry at the real location parses
   identical bytes with identical flags. Only the compiled-source path is rewritten; the
   `-I external/...` include paths stay exactly as the build spells them.

2. THE macOS SDK libc++ (`--system Darwin` only). The `--config=clang` toolchain (toolchains_llvm)
   points libc++ at the Xcode SDK by emitting `-nostdinc++ -cxx-isystem <SDK>/usr/include/c++/v1`,
   while `-resource-dir` is the hermetic clang's. clang-tidy parses with the hermetic clang (see
   `--bcce-compiler`), so that SDK libc++ against a hermetic resource dir is a mismatch that silently
   degrades its analysis (spurious unused-variable / const-correctness findings). The hermetic clang
   finds its OWN libc++ when left to its default search, so drop just those two flags; `-isysroot`
   stays, for the system C headers. Linux never emits the SDK `-cxx-isystem`, hence the gate.
"""

from __future__ import annotations

import argparse
import json
import re
import sys

# `local_path_override(module_name = "x", path = "y")`, tolerating either field order and any
# whitespace. Read from the extras module include rather than hard-coded, so adding an extra needs no
# change here.
_OVERRIDE_BLOCK = re.compile(r"local_path_override\(([^)]*)\)", re.S)
_MODULE_NAME = re.compile(r'module_name\s*=\s*"([^"]+)"')
_PATH = re.compile(r'path\s*=\s*"([^"]+)"')


def local_module_paths(module_bazel: str) -> dict[str, str]:
    """The `module_name -> repo-relative path` map declared by local_path_override calls."""
    overrides = {}
    for block in _OVERRIDE_BLOCK.findall(module_bazel):
        name = _MODULE_NAME.search(block)
        path = _PATH.search(block)
        if name and path:
            overrides[name.group(1)] = path.group(1).rstrip("/")
    return overrides


def external_prefix_map(overrides: dict[str, str]) -> dict[str, str]:
    """Every canonical external-directory spelling bazel has used, mapped to the source directory."""
    remap = {}
    for name, path in overrides.items():
        # `+` is the bzlmod canonical separator; `~` was its predecessor. The bare form covers a
        # WORKSPACE-style repository of the same name.
        for external in (f"external/{name}+/", f"external/{name}~/", f"external/{name}/"):
            remap[external] = f"{path}/"
    return remap


def to_source_path(value: str, remap: dict[str, str]) -> str:
    """`value` with an external local-module prefix replaced by the real source directory."""
    for external, path in remap.items():
        if value.startswith(external):
            return path + value[len(external) :]
    return value


def strip_sdk_libcxx(arguments: list[str]) -> list[str]:
    """`arguments` without `-nostdinc++` and without the SDK's `-cxx-isystem <path>` pair."""
    out: list[str] = []
    skip = False
    for i, arg in enumerate(arguments):
        if skip:
            skip = False
            continue
        if arg == "-nostdinc++":
            continue
        following = arguments[i + 1] if i + 1 < len(arguments) else ""
        if arg == "-cxx-isystem" and following.endswith("/c++/v1") and "MacOSX.sdk" in following:
            skip = True  # also drop the path argument that follows
            continue
        out.append(arg)
    return out


def drop_c_sources(entries: list[dict]) -> tuple[list[dict], int]:
    """Drops C translation units. Returns the kept entries and how many were dropped.

    xff is C++ only - `git ls-files '*.c'` is empty - so every `.c` entry in the database belongs to
    a third-party dependency (bzip2, pcre2, libarchive and the codecs they pull in). Nothing here
    ever lints them: the clang-tidy hook is handed first-party files, and nobody edits vendored C.
    Keeping third-party entries would make tools that walk the whole database lint code outside
    xff's ownership and policy. The language-neutral clang driver now preserves their C language
    during extraction, but dropping them still keeps the database scoped to code we maintain.
    """
    kept = [entry for entry in entries if not entry["file"].endswith(".c")]
    return kept, len(entries) - len(kept)


def fix_entries(entries: list[dict], remap: dict[str, str], system: str) -> int:
    """Applies both passes in place. Returns how many entries had their source path rewritten."""
    rewritten = 0
    for entry in entries:
        source = entry["file"]
        real = to_source_path(source, remap)
        if real != source:
            entry["file"] = real
            # The command names the source too; keep the two in step or the tooling parses the
            # execroot path again and this pass buys nothing.
            entry["arguments"] = [real if arg == source else arg for arg in entry["arguments"]]
            rewritten += 1
        if system == "Darwin":
            entry["arguments"] = strip_sdk_libcxx(entry["arguments"])
    return rewritten


def label_to_execroot_path(label: str) -> str:
    """A bazel source label as the extractor spells the file: `@@repo+//pkg:file` -> `external/repo+/pkg/file`.

    The main repo has an empty repo part (`@@//xff/engine:run.cc`), so it maps to the plain path.
    """
    match = re.match(r"^@@([^/]*)//([^:]*):(.*)$", label.strip())
    if match is None:
        return label.strip()
    repo, package, name = match.groups()
    prefix = "" if not repo else f"external/{repo}/"
    return prefix + (f"{package}/{name}" if package else name)


def missing_from_database(entries: list[dict], labels: list[str], remap: dict[str, str]) -> list[str]:
    """First-party sources bazel compiles that the database has no entry for.

    clang-tidy only lints what the database lists, so a source missing from it is silently unlinted -
    which is not hypothetical: every extras translation unit was absent once (see //:refresh_compile_commands),
    and the symptom was a clean-looking run. The comparison MUST go through `to_source_path`, because the
    labels name `external/xff_archive+/...` while the database has already been rewritten to
    `extra_modules/archive/...`; comparing the raw spellings reports all 42 extras files as missing when
    every one of them is present.
    """
    have = {entry["file"].removeprefix("./") for entry in entries}
    wanted = (to_source_path(label_to_execroot_path(label), remap) for label in labels if label.strip())
    return sorted({path for path in wanted if path not in have})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("database", help="path to compile_commands.json, rewritten in place")
    parser.add_argument(
        "module_bazel",
        nargs="+",
        help="MODULE.bazel files/fragments whose local_path_override declarations are merged",
    )
    parser.add_argument("--system", default="", help="`uname -s`; the macOS pass runs for Darwin")
    parser.add_argument(
        "--expect-sources",
        default="",
        help="file of bazel source labels that MUST have a database entry; a missing one fails the run",
    )
    args = parser.parse_args()

    local_modules = {}
    for module_path in args.module_bazel:
        with open(module_path, encoding="utf-8") as module_file:
            local_modules.update(local_module_paths(module_file.read()))
    remap = external_prefix_map(local_modules)
    with open(args.database, encoding="utf-8") as database:
        entries = json.load(database)
    rewritten = fix_entries(entries, remap, args.system)
    entries, dropped = drop_c_sources(entries)
    with open(args.database, "w", encoding="utf-8") as database:
        json.dump(entries, database, indent=2)
        database.write("\n")
    print(
        f"compile_commands.json: {rewritten} local-module source paths rewritten, "
        f"{dropped} third-party C sources dropped",
        file=sys.stderr,
    )
    if args.expect_sources:
        with open(args.expect_sources, encoding="utf-8") as labels_file:
            labels = sorted({line for line in labels_file.read().splitlines() if line.strip()})
        missing = missing_from_database(entries, labels, remap)
        print(
            f"compile_commands.json: {len(labels)} first-party sources expected, {len(missing)} missing",
            file=sys.stderr,
        )
        if missing:
            print(
                "compile_commands.json: these sources are compiled by bazel but absent from the database, "
                "so clang-tidy silently skips them:",
                file=sys.stderr,
            )
            for path in missing:
                print(f"  {path}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
