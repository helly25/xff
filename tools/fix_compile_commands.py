#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com)
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
   while `-resource-dir` is the hermetic clang's. clang-tidy parses with the hermetic clang++ (see
   `--bcce-compiler`), so that SDK libc++ against a hermetic resource dir is a mismatch that silently
   degrades its analysis (spurious unused-variable / const-correctness findings). The hermetic clang++
   finds its OWN libc++ when left to its default search, so drop just those two flags; `-isysroot`
   stays, for the system C headers. Linux never emits the SDK `-cxx-isystem`, hence the gate.
"""

from __future__ import annotations

import argparse
import json
import re
import sys

# `local_path_override(module_name = "x", path = "y")`, tolerating either field order and any
# whitespace. Read from MODULE.bazel rather than hard-coded, so adding an extra needs no change here.
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("database", help="path to compile_commands.json, rewritten in place")
    parser.add_argument("module_bazel", help="path to MODULE.bazel, read for local_path_override")
    parser.add_argument("--system", default="", help="`uname -s`; the macOS pass runs for Darwin")
    args = parser.parse_args()

    with open(args.module_bazel, encoding="utf-8") as module_file:
        remap = external_prefix_map(local_module_paths(module_file.read()))
    with open(args.database, encoding="utf-8") as database:
        entries = json.load(database)
    rewritten = fix_entries(entries, remap, args.system)
    with open(args.database, "w", encoding="utf-8") as database:
        json.dump(entries, database, indent=2)
        database.write("\n")
    print(f"compile_commands.json: {rewritten} local-module source paths rewritten", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
