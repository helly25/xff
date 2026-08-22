#!/usr/bin/env python3
"""Generate genhtml color thresholds from xff's coverage policy."""

import argparse
import json
from pathlib import Path
from typing import Any


_METRICS = ("line", "function", "branch")
_POLICY_KEYS = {"line": "lines", "function": "functions", "branch": "branches"}
def render(policy: dict[str, Any]) -> str:
    minimum = policy["minimum"]
    lines: list[str] = []
    for metric in _METRICS:
        key = _POLICY_KEYS[metric]
        threshold = int(minimum[key])
        if not 0 <= threshold <= 100:
            raise ValueError(f"{metric} coverage minimum must satisfy 0 <= minimum <= 100: {threshold}")
        # The detailed report uses the same binary pass/fail boundary as the
        # enforcing summary. Setting both limits to the floor removes genhtml's
        # otherwise misleading medium band.
        lines.append(f"genhtml_{metric}_hi_limit = {threshold}")
        lines.append(f"genhtml_{metric}_med_limit = {threshold}")
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("policy", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_text(render(json.loads(args.policy.read_text(encoding="utf-8"))), encoding="utf-8")


if __name__ == "__main__":
    main()
