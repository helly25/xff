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

"""The composable extras, derived from bazelmod/extras.MODULE.bazel.

    tools/extras.py --wildcards   # @xff_archive//... @xff_pcre2//...
    tools/extras.py --modules     # xff_archive xff_pcre2
    tools/extras.py --instrumentation-filter

Every extra is a separate bazel MODULE under `extra_modules/`, declared in the extras include with a
`bazel_dep` + `local_path_override` pair. That makes the include the single source of truth for
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


def instrumentation_filter(declared: dict[str, str]) -> str:
    """The Bazel coverage instrumentation filter for core plus all extras."""
    escaped = "|".join(re.escape(module) for module in declared)
    extras_filter = f",@@({escaped})[+]//" if escaped else ""
    return f"//xff[/:],//xff_extras_api[/:]{extras_filter}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--wildcards", action="store_true", help="print `@module//...` for each extra")
    group.add_argument("--modules", action="store_true", help="print each extra's module name")
    group.add_argument(
        "--instrumentation-filter",
        action="store_true",
        help="print the Bazel coverage filter for the core and every declared extra",
    )
    args = parser.parse_args()

    # An empty result is legitimate for a stripped extras include. Callers word-split this, so
    # printing nothing is the right answer.
    declared = extras((_REPO_ROOT / "bazelmod" / "extras.MODULE.bazel").read_text(encoding="utf-8"))

    if args.wildcards:
        print(" ".join(f"@{module}//..." for module in declared))
    elif args.modules:
        print(" ".join(declared))
    else:
        print(instrumentation_filter(declared))
    return 0


if __name__ == "__main__":
    sys.exit(main())
