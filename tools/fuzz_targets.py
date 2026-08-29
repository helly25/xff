#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
# SPDX-License-Identifier: Apache-2.0

"""Discover and run every first-party fuzz campaign.

    tools/fuzz_targets.py --list
    tools/fuzz_targets.py --campaign-seconds=60

The BUILD files are the single source of truth. Core targets use ordinary labels; targets in a
composable extra are translated to that extra's module label through bazelmod/extras.MODULE.bazel.
This keeps scheduled campaigns complete when a new cc_fuzz_test is added.

The driver controls time and toolchain setup, not a target's capabilities. Harnesses that evaluate
arbitrary expressions must follow AGENTS.md's fuzz-test isolation rules; a command-line `--safe`
flag is not a substitute for an in-memory filesystem and safety-class filtering inside the harness.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import subprocess
import sys

import extras

_REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
_NAME = re.compile(r'\bname\s*=\s*"([^"]+)"')


def rule_blocks(source: str, rule: str) -> list[str]:
    """Returns balanced call bodies for `rule(...)`, ignoring parentheses inside strings."""
    blocks: list[str] = []
    marker = f"{rule}("
    start = 0
    while (found := source.find(marker, start)) >= 0:
        pos = found + len(marker)
        depth = 1
        quote = ""
        escaped = False
        while pos < len(source) and depth:
            char = source[pos]
            if quote:
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif char == quote:
                    quote = ""
            elif char in "\"'":
                quote = char
            elif char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            pos += 1
        if depth:
            raise ValueError(f"unterminated {rule} call")
        blocks.append(source[found + len(marker) : pos - 1])
        start = pos
    return blocks


def discover_targets(root: pathlib.Path) -> list[str]:
    """Returns the stable list of generated `*_run` campaign labels below `root`."""
    declared = extras.extras((root / "bazelmod" / "extras.MODULE.bazel").read_text(encoding="utf-8"))
    extra_by_path = {pathlib.PurePosixPath(path): module for module, path in declared.items()}
    targets: list[str] = []
    for build in root.rglob("BUILD.bazel"):
        relative = build.relative_to(root)
        if any(part.startswith("bazel-") or part in {".git", ".cache"} for part in relative.parts):
            continue
        package = relative.parent
        owner: tuple[pathlib.PurePosixPath, str] | None = next(
            ((path, module) for path, module in extra_by_path.items() if package == path or path in package.parents),
            None,
        )
        for block in rule_blocks(build.read_text(encoding="utf-8"), "cc_fuzz_test"):
            match = _NAME.search(block)
            if match is None:
                raise ValueError(f"cc_fuzz_test without a literal name in {relative}")
            name = f"{match.group(1)}_run"
            if owner is None:
                targets.append(f"//{package.as_posix()}:{name}")
            else:
                extra_path, module = owner
                module_package = package.relative_to(extra_path).as_posix()
                prefix = f"@{module}//{module_package}" if module_package != "." else f"@{module}//"
                targets.append(f"{prefix}:{name}")
    return sorted(targets)


def campaign_environment() -> dict[str, str]:
    """Returns the fuzz environment, including the required Darwin runtime compatibility setting."""
    environment = dict(os.environ)
    if sys.platform == "darwin":
        # The hermetic libc++ annotates vector capacity, while libFuzzer's own runtime is not built
        # with matching annotations. Disable only that incompatible runtime check; ASan's ordinary
        # heap, stack, bounds, and lifetime checks remain active for xff.
        options = [part for part in environment.get("ASAN_OPTIONS", "").split(":") if part]
        options = [part for part in options if not part.startswith("detect_container_overflow=")]
        options.append("detect_container_overflow=0")
        environment["ASAN_OPTIONS"] = ":".join(options)
    return environment


def run_campaigns(targets: list[str], seconds: int) -> int:
    """Runs each campaign for a bounded duration, stopping at the first failed correctness test."""
    environment = campaign_environment()
    for target in targets:
        result = subprocess.run(
            [
                "bazel",
                "run",
                "-c",
                "opt",
                "--config=xff_docs",
                "--config=fuzz",
                target,
                "--",
                "--clean",
                f"--timeout_secs={seconds}",
            ],
            check=False,
            env=environment,
        )
        if result.returncode:
            return result.returncode
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--list", action="store_true", help="print every generated campaign label")
    mode.add_argument("--campaign-seconds", type=int, metavar="N", help="run every campaign for N seconds")
    args = parser.parse_args()
    targets = discover_targets(_REPO_ROOT)
    if args.list:
        print("\n".join(targets))
        return 0
    if args.campaign_seconds <= 0:
        parser.error("--campaign-seconds must be positive")
    return run_campaigns(targets, args.campaign_seconds)


if __name__ == "__main__":
    sys.exit(main())
