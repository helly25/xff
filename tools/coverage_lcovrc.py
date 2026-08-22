#!/usr/bin/env python3
"""Generate genhtml color thresholds from xff's coverage policy."""

import argparse
import json
from pathlib import Path
from typing import Any


_METRICS = ("line", "function", "branch")
_POLICY_KEYS = {"line": "lines", "function": "functions", "branch": "branches"}
_WARNING_GAP = 15


def render(policy: dict[str, Any]) -> str:
    minimum = policy["minimum"]
    lines: list[str] = []
    for metric in _METRICS:
        high = int(minimum[_POLICY_KEYS[metric]])
        if not 0 <= high <= 100:
            raise ValueError(f"minimum {metric} coverage must be between 0 and 100: {high}")
        lines.append(f"genhtml_{metric}_hi_limit = {high}")
        lines.append(f"genhtml_{metric}_med_limit = {max(0, high - _WARNING_GAP)}")
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("policy", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_text(render(json.loads(args.policy.read_text(encoding="utf-8"))), encoding="utf-8")


if __name__ == "__main__":
    main()
