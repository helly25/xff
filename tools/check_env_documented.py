#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
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
"""Guard: every environment variable xff READS is documented in `--help=environment`.

The read sites are `xff::env::Get("NAME")` / `env::Has("NAME")` calls scattered across the
codebase; the documentation is the hand-authored `kVars` table in `xff/cli/help_build.cc`,
whose rows the `--help=environment` topic renders. Nothing tied the two together, so
`XDG_RUNTIME_DIR` was read by the extractor while the topic that promises to list what xff
reads did not mention it.

A row may name several variables at once (`LC_ALL, LC_CTYPE, LANG`), which is how the locale
trio reads best, so the row's term is split on commas before comparing.

Reported both ways: a name read but not documented is the bug this exists for, and a name
documented but never read is a stale promise (or a typo in either place).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# `env::Get("NAME")`, `env::Has("NAME")`, and the string literals of the prewarm table.
_READ = re.compile(r'env::(?:Get|Has)\(\s*"([A-Za-z_][A-Za-z0-9_]*)"')
# The prewarm table, whether it is written as a sized array (`= {...}`) or a deduced one
# (`= std::to_array<std::string_view>({...})`); the latter is preferred, since a hand-written size
# silently pads the array when it drifts.
_PREWARM_BLOCK = re.compile(r"kKnownEnv\s*=\s*(?:std::to_array<[^>]*>\()?\s*\{(.*?)\}", re.DOTALL)
_DOC_BLOCK = re.compile(r"kVars\s*=\s*std::to_array<DocPair>\(\{(.*?)\}\);", re.DOTALL)
_DOC_TERM = re.compile(r'\{\s*"([^"]+)"')
_LITERAL = re.compile(r'"([A-Za-z_][A-Za-z0-9_]*)"')

# Names a user supplies rather than xff reading a fixed variable: the `{env.NAME}` field
# vocabulary reads whatever the template names, which is documented as a mechanism instead.
_DYNAMIC_OK = frozenset()


def _sources(root: Path) -> list[Path]:
    files = []
    for directory in ("xff", "xff_extras_api", "extra_modules"):
        base = root / directory
        if not base.is_dir():
            continue
        for path in base.rglob("*.cc"):
            if path.name.endswith("_test.cc"):
                continue
            files.append(path)
        for path in base.rglob("*.h"):
            files.append(path)
    return files


def main(argv: list[str]) -> int:
    root = Path(argv[1]) if len(argv) > 1 else Path()
    # Three DISTINCT sets, deliberately: an earlier version folded the prewarm table into `read`,
    # which made the "is the known set complete" check below vacuous - a prewarmed name counted as a
    # read of itself, so it could never be missing.
    read: set[str] = set()  # env::Get("X") / env::Has("X") literals: what the code demonstrably reads
    prewarmed: set[str] = set()  # the kKnownEnv table: what a run claims to read up front
    for path in _sources(root):
        read.update(_READ.findall(path.read_text(encoding="utf-8")))
    main_cc = root / "xff/cli/main.cc"
    if main_cc.is_file():
        for block in _PREWARM_BLOCK.findall(main_cc.read_text(encoding="utf-8")):
            prewarmed.update(_LITERAL.findall(block))

    help_build = root / "xff/cli/help_build.cc"
    if not help_build.is_file():
        print(f"{help_build}: not found, cannot check the environment documentation", file=sys.stderr)
        return 1
    documented: set[str] = set()
    for block in _DOC_BLOCK.findall(help_build.read_text(encoding="utf-8")):
        for term in _DOC_TERM.findall(block):
            documented.update(name.strip() for name in term.split(","))

    status = 0
    # 1. Every name the code reads is documented - the promise --help=environment makes.
    for name in sorted(read - documented - _DYNAMIC_OK):
        print(
            f"{name}: read by the code but missing from the --help=environment table "
            f"(add a row to kVars in xff/cli/help_build.cc)",
            file=sys.stderr,
        )
        status = 1
    # 2. Every documented name is read somewhere, or at least declared in the prewarm table (a name
    #    read through a loop over a named array reaches the code that way rather than as a literal).
    for name in sorted(documented - read - prewarmed):
        print(
            f"{name}: documented in --help=environment but neither read nor prewarmed "
            f"(remove the kVars row, or fix the name on either side)",
            file=sys.stderr,
        )
        status = 1
    # 3. The prewarm table claims to BE the set a run reads, and it drifted twice: XFF_MANPAGER was
    #    never in it, and LS_COLORS was lost to a hand-written array size that no longer matched the
    #    contents. A missing name still works (env::Get caches lazily), so this is hygiene, not a bug.
    for name in sorted(read - prewarmed - _DYNAMIC_OK):
        print(
            f"{name}: read by the code but missing from the kKnownEnv prewarm table in xff/cli/main.cc "
            f"(that table is meant to be the complete set a run reads)",
            file=sys.stderr,
        )
        status = 1
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv))
