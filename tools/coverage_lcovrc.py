#!/usr/bin/env python3
"""Generate genhtml color thresholds from xff's coverage policy."""

import argparse
import json
from pathlib import Path
from typing import Any


_METRICS = ("line", "function", "branch")
_POLICY_KEYS = {"line": "lines", "function": "functions", "branch": "branches"}


def _bands(policy: dict[str, Any]) -> tuple[dict[str, Any], dict[str, Any]]:
    bands = policy.get("bands")
    if bands is not None:
        return bands["medium"], bands["high"]
    minimum = policy["minimum"]
    return minimum, {**minimum, **policy.get("target", {})}


def render(policy: dict[str, Any]) -> str:
    medium_band, high_band = _bands(policy)
    lines: list[str] = []
    for metric in _METRICS:
        key = _POLICY_KEYS[metric]
        medium = int(medium_band[key])
        high = int(high_band[key])
        if not 0 <= medium <= high <= 100:
            raise ValueError(
                f"{metric} coverage thresholds must satisfy 0 <= minimum <= target <= 100: "
                f"{medium}, {high}"
            )
        lines.append(f"genhtml_{metric}_hi_limit = {high}")
        lines.append(f"genhtml_{metric}_med_limit = {medium}")
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("policy", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_text(render(json.loads(args.policy.read_text(encoding="utf-8"))), encoding="utf-8")


if __name__ == "__main__":
    main()
