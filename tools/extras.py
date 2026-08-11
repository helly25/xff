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

"""The composable extras, derived from MODULE.bazel. Also the `check-extras-covered` hook.

    tools/extras.py --wildcards   # @xff_archive//... @xff_pcre2//...
    tools/extras.py --modules     # xff_archive xff_pcre2
    tools/extras.py --check       # fail if a consumer's hard-coded extras list has drifted

Every extra is a separate bazel MODULE under `extra_modules/`, declared in MODULE.bazel with a
`bazel_dep` + `local_path_override` pair. That makes MODULE.bazel the single source of truth for
WHICH extras exist, and this tool the way consumers ask, instead of each keeping its own list.

Why it exists: `bazel test //...` does NOT match another bazel module, so an extra whose wildcard is
missing from a CI command has no coverage at all - not its tests, not its build. `@xff_archive` was
missing from all three test cells until 2026-08-11, which is how a PR merged green with four of its
test fixtures absent from the repo. Enumerating by hand in three workflow steps only postpones the
next instance of that, so CI asks this tool instead.

One consumer genuinely cannot: `//:refresh_compile_commands` needs each extra's BUILD FLAG as well
(`--//xff:xff_archive`), and a flag name is not derivable from a module name - `xff_pcre2`'s flag is
`--//xff:xff_pcre`. Its list stays literal Starlark and `--check` asserts it still covers exactly the
extras MODULE.bazel declares, so adding an extra fails loudly instead of silently dropping it from
clang-tidy's view.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

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


def compile_db_modules(build_bazel: str) -> set[str]:
    """The modules named in //:refresh_compile_commands's `targets` dict."""
    return set(re.findall(r'"@(\w+)//\.\.\."', build_bazel))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--wildcards", action="store_true", help="print `@module//...` for each extra")
    group.add_argument("--modules", action="store_true", help="print each extra's module name")
    group.add_argument("--check", action="store_true", help="verify hard-coded lists still match")
    args = parser.parse_args()

    declared = extras((_REPO_ROOT / "MODULE.bazel").read_text(encoding="utf-8"))
    if not declared:
        print("no extras found in MODULE.bazel; the local_path_override parse must have broken", file=sys.stderr)
        return 1

    if args.wildcards:
        print(" ".join(f"@{module}//..." for module in declared))
        return 0
    if args.modules:
        print(" ".join(declared))
        return 0

    # --check: the one consumer that cannot derive its list, because it also needs each extra's build
    # flag, which is not derivable from the module name.
    in_compile_db = compile_db_modules((_REPO_ROOT / "BUILD.bazel").read_text(encoding="utf-8"))
    missing = set(declared) - in_compile_db
    if missing:
        print(
            "//:refresh_compile_commands does not cover these extras: "
            + ", ".join(sorted(missing))
            + "\nAdd each with its build flag, e.g. `\"@xff_archive//...\": \"--//xff:xff_archive\"`,"
            " or clang-tidy and clangd will silently not see the extra at all.",
            file=sys.stderr,
        )
        return 1
    stale = in_compile_db - set(declared)
    if stale:
        print(
            "//:refresh_compile_commands names modules MODULE.bazel no longer declares as extras: "
            + ", ".join(sorted(stale)),
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
