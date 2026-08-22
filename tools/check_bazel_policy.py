#!/usr/bin/env python3
"""Checks repository-wide Bazel package and C++ analysis policy."""

from __future__ import annotations

import pathlib
import re
import sys


_PRIVATE_PACKAGE = re.compile(
    r'\bpackage\s*\(\s*default_visibility\s*=\s*\[\s*"//visibility:private"\s*\]\s*\)',
    re.DOTALL,
)
_REQUIRED_FEATURES = frozenset({"layering_check", "parse_headers"})


def find_build_files(root: pathlib.Path) -> list[pathlib.Path]:
    return sorted(
        path
        for path in root.rglob("*")
        if path.is_file()
        and path.name in {"BUILD", "BUILD.bazel"}
        and not any(part.startswith("bazel-") or part == ".git" for part in path.parts)
    )


def enabled_features(bazelrc: str, option: str) -> set[str]:
    enabled: set[str] = set()
    pattern = re.compile(rf"^common\s+.*?--{re.escape(option)}=([^\s#]+)", re.MULTILINE)
    for value in pattern.findall(bazelrc):
        for feature in value.split(","):
            if feature.startswith("-"):
                enabled.discard(feature[1:])
            else:
                enabled.add(feature)
    return enabled


def violations(root: pathlib.Path) -> list[str]:
    errors = [
        f"{path.relative_to(root)}: package default_visibility must be //visibility:private"
        for path in find_build_files(root)
        if _PRIVATE_PACKAGE.search(path.read_text(encoding="utf-8")) is None
    ]
    bazelrc = (root / ".bazelrc").read_text(encoding="utf-8")
    for option in ("features", "host_features"):
        missing = _REQUIRED_FEATURES - enabled_features(bazelrc, option)
        if missing:
            errors.append(f".bazelrc: --{option} is missing: {', '.join(sorted(missing))}")
    return errors


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    errors = violations(root)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
