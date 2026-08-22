#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com)
# SPDX-License-Identifier: Apache-2.0

"""Map Bazel external-repository LCOV paths back to checked-in extra modules."""

from __future__ import annotations

import argparse
import fnmatch
import json
import re
from pathlib import Path

import extras


def declared_extras() -> dict[str, str]:
    """Returns every extra from the repository's single source of truth."""
    root = Path(__file__).resolve().parent.parent
    return extras.extras((root / "bazelmod" / "extras.MODULE.bazel").read_text(encoding="utf-8"))


def remap(report: str, modules: dict[str, str]) -> str:
    """Returns an LCOV report whose declared extra sources use workspace paths."""
    for module, path in modules.items():
        pattern = rf"(?m)^SF:external/{re.escape(module)}\+/(.*)$"
        report = re.sub(pattern, rf"SF:{path}/\1", report)
    return report


def _matches(path: str, patterns: list[str]) -> bool:
    return any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)


def _slug(group: str, name: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", f"{group}-{name}".lower()).strip("-")


def grouped(report: str, modules: dict[str, str], policy: dict, source_root: Path) -> str:
    """Groups first-party records by policy category and links their source files."""
    categories = [
        (_slug(group, name), category["include"])
        for group, entries in policy.get("categories", {}).items()
        for name, category in entries.items()
    ]
    includes = policy.get("include", ["xff/**"])
    excludes = policy.get("exclude", [])
    physical_to_logical = tuple((path.rstrip("/") + "/", module + "/") for module, path in modules.items())
    result = []
    for record in report.split("end_of_record\n"):
        match = re.search(r"(?m)^SF:(.+)$", record)
        if not match:
            continue
        physical = match.group(1)
        logical = physical
        for prefix, replacement in physical_to_logical:
            if physical.startswith(prefix):
                logical = replacement + physical.removeprefix(prefix)
                break
        if not _matches(logical, includes) or _matches(logical, excludes):
            continue
        matches = [slug for slug, patterns in categories if _matches(logical, patterns)]
        if len(matches) != 1:
            raise ValueError(f"coverage source {logical!r} belongs to {len(matches)} policy categories")
        linked = source_root.resolve() / matches[0] / logical
        linked.parent.mkdir(parents=True, exist_ok=True)
        if not linked.exists() and not linked.is_symlink():
            linked.symlink_to((Path.cwd() / physical).resolve())
        result.append(record.replace(f"SF:{physical}", f"SF:{linked}", 1) + "end_of_record\n")
    return "".join(result)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--policy", type=Path)
    parser.add_argument("--source-root", type=Path)
    args = parser.parse_args()
    modules = declared_extras()
    report = remap(args.input.read_text(encoding="utf-8"), modules)
    if args.policy or args.source_root:
        if not args.policy or not args.source_root:
            parser.error("--policy and --source-root must be specified together")
        report = grouped(
            report,
            modules,
            json.loads(args.policy.read_text(encoding="utf-8")),
            args.source_root,
        )
    args.output.write_text(report, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
