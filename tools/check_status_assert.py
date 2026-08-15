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

"""Fails on `ASSERT_THAT(x, IsOk())` followed by a dereference of `x`.

STYLE_CPP.md has said to prefer `MBO_ASSERT_OK_AND_ASSIGN` / `IsOkAndHolds` over asserting a
`StatusOr` is OK and then dereferencing it since the status-matcher section was written, and
CLAUDE.md makes that binding. It was still violated 78 times across 10 files, which is what this
hook is for: the rule is mechanical, so a mechanical check holds it where prose did not.

What is wrong with the pattern: `ASSERT_THAT(so, IsOk()); ... *so ...` splits one fact over two
statements, and every later use has to re-dereference an optional-like value whose validity is only
guaranteed by an assertion several lines up. `MBO_ASSERT_OK_AND_ASSIGN(const T value, expr)` binds
the value once, and `IsOkAndHolds(m)` says the whole thing in a single matcher when the value is
only inspected once.

The check is deliberately narrow so it cannot cry wolf:
  - only `*_test.cc` files, only `ASSERT_THAT(<identifier>, IsOk())` (the bare-variable form),
  - only when the SAME identifier is dereferenced (`*x`, `x->`) within the following few lines.
An `ASSERT_THAT(SomeCall(), IsOk())` on a `Status`, or one whose value is never dereferenced, is
untouched - those are correct uses.
"""

from __future__ import annotations

import re
import sys

# `ASSERT_THAT(name, IsOk())`, optionally with a trailing `<< something` streamed message.
_ASSERT = re.compile(r"\bASSERT_THAT\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*IsOk\(\)\s*\)")

# How far after the assertion a dereference still counts as "this is that pattern". Small, because a
# dereference much later is usually a different variable's story.
_WINDOW = 8


def _dereferences(name: str, text: str) -> bool:
    """Whether `text` dereferences `name` as a StatusOr (`*name` or `name->`)."""
    escaped = re.escape(name)
    return bool(re.search(rf"\*{escaped}\b", text) or re.search(rf"\b{escaped}->", text))


def check(path: str) -> list[str]:
    """Returns one message per offending line in `path`."""
    with open(path, encoding="utf-8") as handle:
        lines = handle.read().split("\n")
    problems: list[str] = []
    for index, line in enumerate(lines):
        found = _ASSERT.search(line)
        if not found:
            continue
        name = found.group(1)
        window = "\n".join(lines[index + 1 : index + 1 + _WINDOW])
        if _dereferences(name, window):
            problems.append(
                f"{path}:{index + 1}: ASSERT_THAT({name}, IsOk()) then dereferences `{name}`; "
                f"use MBO_ASSERT_OK_AND_ASSIGN(const T {name}, ...) to bind it once, or a single "
                f"EXPECT_THAT(..., IsOkAndHolds(m)) when it is only inspected once (STYLE_CPP.md)"
            )
    return problems


def main(argv: list[str]) -> int:
    problems: list[str] = []
    for path in argv[1:]:
        if path.endswith("_test.cc"):
            problems.extend(check(path))
    for problem in problems:
        print(problem, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
