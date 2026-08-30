#!/usr/bin/env python3
"""Rejects unannotated host-file I/O in xff production C++ sources."""

from __future__ import annotations

import pathlib
import re
import sys


MARKER = "XFF_HOST_IO:"
PATTERNS = (
    re.compile(r"\bstd::(?:i|o|io)fstream\b"),
    re.compile(r"\bmbo::file::Artefact::Read(?:MaxLines)?\s*\("),
    re.compile(r"\b(?:fopen|freopen|fclose|fread|fwrite)\s*\("),
    re.compile(
        r"\b(?:std::filesystem|stdfs)::(?:create_directory|create_directories|remove|remove_all|rename|status)\s*\("
    ),
)


def check(path: pathlib.Path) -> list[str]:
    if path.name.endswith("_test.cc") or path.name.endswith("_test.h"):
        return []
    lines = path.read_text(encoding="utf-8").splitlines()
    errors: list[str] = []
    for number, line in enumerate(lines, start=1):
        if not any(pattern.search(line) for pattern in PATTERNS):
            continue
        previous = lines[number - 2] if number > 1 else ""
        if MARKER not in previous and MARKER not in line:
            errors.append(f"{path}:{number}: unannotated host file I/O (add {MARKER} <reason>)")
    return errors


def main(argv: list[str]) -> int:
    paths = [pathlib.Path(value) for value in argv]
    errors = [error for path in paths for error in check(path)]
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
