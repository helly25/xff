#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
# SPDX-License-Identifier: Apache-2.0
"""Enforce ownership and absolute qualification of global namespaces."""

from __future__ import annotations

import re
import sys
from pathlib import Path


_STD_NAMESPACE = re.compile(
    r"\bnamespace\s+(?:[A-Za-z_]\w*\s*::\s*)*std\s*(?:\{|=)"
)
_USING_STD_NAMESPACE = re.compile(r"\busing\s+namespace\s+(?:::)?std\b")
_RELATIVE_GLOBAL_USING = re.compile(
    r"\busing\s+(?:absl|mbo|std|testing|xff)::"
)
_RELATIVE_GLOBAL_ALIAS = re.compile(
    r"\bnamespace\s+[A-Za-z_]\w*\s*=\s*(?:absl|mbo|std|testing|xff)::"
)
_SPLIT_GLOBAL_ALIASES = re.compile(
    r"(?:using\s+::(?:absl|mbo|std|testing|xff)::[^;]+;|"
    r"namespace\s+[A-Za-z_]\w*\s*=\s*::(?:absl|mbo|std|testing|xff)::[^;]+;)"
    r"\n[ \t]*\n[ \t]*"
    r"(?:using\s+::(?:absl|mbo|std|testing|xff)::|"
    r"namespace\s+[A-Za-z_]\w*\s*=\s*::(?:absl|mbo|std|testing|xff)::)"
)


def _mask_comments_and_literals(source: str) -> str:
    """Replaces comments and quoted literals while preserving offsets and lines."""
    masked = list(source)
    index = 0
    size = len(source)
    while index < size:
        if source.startswith("//", index):
            end = source.find("\n", index)
            end = size if end < 0 else end
        elif source.startswith("/*", index):
            close = source.find("*/", index + 2)
            end = size if close < 0 else close + 2
        elif source[index] in {'"', "'"}:
            quote = source[index]
            end = index + 1
            while end < size:
                if source[end] == "\\":
                    end += 2
                elif source[end] == quote:
                    end += 1
                    break
                else:
                    end += 1
        else:
            index += 1
            continue
        for position in range(index, min(end, size)):
            if masked[position] != "\n":
                masked[position] = " "
        index = end
    return "".join(masked)


def check(path: Path) -> list[str]:
    source = path.read_text(encoding="utf-8")
    masked = _mask_comments_and_literals(source)
    errors = []
    for pattern in (_STD_NAMESPACE, _USING_STD_NAMESPACE):
        for match in pattern.finditer(masked):
            line = source.count("\n", 0, match.start()) + 1
            errors.append(
                f"{path}:{line}: do not declare/reopen a namespace named std or use "
                "a using-directive for ::std; qualify standard-library names explicitly"
            )
    for pattern in (_RELATIVE_GLOBAL_USING, _RELATIVE_GLOBAL_ALIAS):
        for match in pattern.finditer(masked):
            line = source.count("\n", 0, match.start()) + 1
            errors.append(
                f"{path}:{line}: globally rooted using-declarations and namespace aliases "
                "require a leading ::"
            )
    for match in _SPLIT_GLOBAL_ALIASES.finditer(masked):
        line = source.count("\n", 0, match.start()) + 1
        errors.append(
            f"{path}:{line}: globally rooted using-declarations and namespace aliases "
            "belong in one block without library-specific blank lines"
        )
    return errors


def main(argv: list[str]) -> int:
    errors = [error for name in argv[1:] for error in check(Path(name))]
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
