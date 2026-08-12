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

"""The composable extras, derived from MODULE.bazel.

    tools/extras.py --wildcards        # @xff_archive//... @xff_pcre2//...
    tools/extras.py --modules          # xff_archive xff_pcre2
    tools/extras.py --check-configured # every declared extra is wired + in --config=xff_docs

Every extra is a separate bazel MODULE under `extra_modules/`, declared in MODULE.bazel with a
`bazel_dep` + `local_path_override` pair. That makes MODULE.bazel the single source of truth for
WHICH extras exist, and this tool the way consumers ask, instead of each keeping its own list.

Why it exists: `bazel test //...` does NOT match another bazel module, so an extra whose wildcard is
missing from a CI command has no coverage at all - not its tests, not its build. `@xff_archive` was
missing from all three test cells until 2026-08-11, which is how a PR merged green with four of its
test fixtures absent from the repo. Enumerating by hand in three workflow steps only postpones the
next instance of that, so CI asks this tool instead.

Starlark cannot read MODULE.bazel from a BUILD file, so //:refresh_compile_commands derives the same
list through a repository rule instead (tools/extras_repo.bzl). Two readers of one source of truth,
rather than a list anyone has to remember: no per-extra BUILD FLAG is needed for either, because
`--//xff:xff_<extra>` gates whether the CORE links an extra, not whether the extra itself builds.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

# `struct(module = "x", flag = "y", ...)` entries of XFF_EXTRAS in xff/extras.bzl. That list is where
# an extra's build flag is paired with its module, because the two names differ (`xff_pcre2` is gated
# by `--//xff:xff_pcre`), and everything else in the build derives from it.
_EXTRAS_STRUCT = re.compile(r"struct\(\s*module\s*=\s*\"([^\"]+)\",\s*flag\s*=\s*\"([^\"]+)\"", re.S)

# `local_path_override(module_name = "x", path = "y")`, in either field order.
_OVERRIDE_BLOCK = re.compile(r"local_path_override\(([^)]*)\)", re.S)
_MODULE_NAME = re.compile(r'module_name\s*=\s*"([^"]+)"')
_PATH = re.compile(r'path\s*=\s*"([^"]+)"')

# The directory that holds the removable extras. `xff_extras_api` is also a local module but is NOT an
# extra: it is the always-built shared seam, deliberately at the top level so a minimal source archive
# can delete extra_modules/ wholesale.
_EXTRAS_DIR = "extra_modules/"

_REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent


def extras(module_bazel: str) -> dict[str, str]:
    """The `module_name -> path` map of every extra declared under extra_modules/."""
    found = {}
    for block in _OVERRIDE_BLOCK.findall(module_bazel):
        name = _MODULE_NAME.search(block)
        path = _PATH.search(block)
        if name and path and path.group(1).startswith(_EXTRAS_DIR):
            found[name.group(1)] = path.group(1).rstrip("/")
    return dict(sorted(found.items()))


def wired_flags(extras_bzl: str) -> dict[str, str]:
    """The `module_name -> build flag` map XFF_EXTRAS declares in xff/extras.bzl."""
    return dict(sorted(_EXTRAS_STRUCT.findall(extras_bzl)))


def check_configured(module_bazel: str, extras_bzl: str, bazelrc: str) -> list[str]:
    """Complaints about extras that are declared but not fully wired. Empty means all good.

    Three things have to agree, and only the first two can be derived by Starlark: MODULE.bazel says
    which extras EXIST, XFF_EXTRAS pairs each with its build flag (and generates the flags, the
    conditions, `:full_build` and `//xff:all_extras_cc` from that), and `.bazelrc` turns them all on
    for `--config=xff_docs`, the config that must document the FULL surface. A `.bazelrc` cannot loop,
    so that last line is the one thing written by hand - and this is what makes forgetting it fail.
    """
    declared = extras(module_bazel)
    wired = wired_flags(extras_bzl)
    docs_flags = set(re.findall(r"^common:xff_docs\s+--//xff:(\S+?)=True", bazelrc, re.M))
    full_flags = set(re.findall(r"^common:xff_full\s+--//xff:(\S+?)=True", bazelrc, re.M))
    enabled = docs_flags | full_flags  # xff_docs inherits xff_full, so either line counts

    complaints = []
    for module in declared:
        if module not in wired:
            complaints.append(
                f"extra '{module}' is declared in MODULE.bazel but missing from XFF_EXTRAS in "
                f"xff/extras.bzl - add struct(module = \"{module}\", flag = ..., target = ...)"
            )
            continue
        if wired[module] not in enabled:
            complaints.append(
                f"extra '{module}' is not enabled by --config=xff_docs - add "
                f"`common:xff_docs --//xff:{wired[module]}=True` to .bazelrc, or the committed XFF.md "
                f"documents less than the full surface"
            )
    for module in wired:
        if module not in declared:
            complaints.append(
                f"XFF_EXTRAS names '{module}', which MODULE.bazel does not declare under extra_modules/"
            )
    return complaints


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--wildcards", action="store_true", help="print `@module//...` for each extra")
    group.add_argument("--modules", action="store_true", help="print each extra's module name")
    group.add_argument(
        "--check-configured",
        action="store_true",
        help="verify every declared extra is in XFF_EXTRAS and enabled by --config=xff_docs",
    )
    args = parser.parse_args()

    # An empty result is legitimate: a minimal source tree has extra_modules/ deleted and the extras
    # stripped from MODULE.bazel (the `minimal` CI cell does exactly that), and then there is nothing to
    # name. Callers word-split this, so printing nothing is the right answer.
    declared_text = (_REPO_ROOT / "MODULE.bazel").read_text(encoding="utf-8")
    declared = extras(declared_text)

    if args.check_configured:
        complaints = check_configured(
            declared_text,
            (_REPO_ROOT / "xff" / "extras.bzl").read_text(encoding="utf-8"),
            (_REPO_ROOT / ".bazelrc").read_text(encoding="utf-8"),
        )
        for complaint in complaints:
            print(f"tools/extras.py: {complaint}", file=sys.stderr)
        return 1 if complaints else 0

    if args.wildcards:
        print(" ".join(f"@{module}//..." for module in declared))
    else:
        print(" ".join(declared))
    return 0


if __name__ == "__main__":
    sys.exit(main())
