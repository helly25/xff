#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com)
# SPDX-License-Identifier: Apache-2.0

"""Map Bazel external-repository LCOV paths back to checked-in extra modules."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

import extras


def remap(report: str, modules: dict[str, str]) -> str:
    """Returns an LCOV report whose declared extra sources use workspace paths."""
    for module, path in modules.items():
        pattern = rf"(?m)^SF:external/{re.escape(module)}\+/(.*)$"
        report = re.sub(pattern, rf"SF:{path}/\1", report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    modules = extras.extras((root / "bazelmod" / "extras.MODULE.bazel").read_text(encoding="utf-8"))
    args.output.write_text(remap(args.input.read_text(encoding="utf-8"), modules), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
