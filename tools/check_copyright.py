#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Require the repository's canonical copyright identity everywhere it is stated."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

COPYRIGHT = "Copyright (c) M. Boerger, the MBO Works authors"
LICENSE_COPYRIGHT = "Copyright M. Boerger, the MBO Works authors"
_SPDX = re.compile(r"SPDX-" r"FileCopyrightText:\s*(.*?)(?:\s*-->)?\s*$")


def check(root: Path, paths: list[Path]) -> list[str]:
    errors: list[str] = []
    for relative in paths:
        path = root / relative
        if not path.is_file():
            continue
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except UnicodeDecodeError:
            continue
        for line_number, line in enumerate(lines, 1):
            match = _SPDX.search(line)
            if match is not None and match.group(1) != COPYRIGHT:
                errors.append(
                    f"{relative}:{line_number}: copyright is {match.group(1)!r}; expected {COPYRIGHT!r}"
                )

    license_path = root / "LICENSE"
    if license_path.is_file():
        lines = license_path.read_text(encoding="utf-8").splitlines()
        declarations = [line for line in lines if line.startswith("Copyright ")]
        if declarations != [LICENSE_COPYRIGHT]:
            errors.append(
                f"LICENSE: copyright declarations are {declarations!r}; expected [{LICENSE_COPYRIGHT!r}]"
            )
    return errors


def _tracked_paths(root: Path) -> list[Path]:
    output = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=root,
        check=True,
        capture_output=True,
    ).stdout
    return [Path(item.decode()) for item in output.split(b"\0") if item]


def main(argv: list[str]) -> int:
    root = Path(argv[1]) if len(argv) > 1 else Path()
    errors = check(root, _tracked_paths(root))
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
