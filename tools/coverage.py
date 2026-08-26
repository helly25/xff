#!/usr/bin/env python3
"""Apply the repository coverage policy to a Bazel LCOV report."""

from __future__ import annotations

import argparse
import fnmatch
import json
import re
import subprocess
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

import coverage_sources
import coverage_policy


@dataclass
class FileCoverage:
    lines: dict[int, int] = field(default_factory=dict)
    functions: list[tuple[int, int]] = field(default_factory=list)
    branches: list[tuple[int, bool]] = field(default_factory=list)


def _repo_path(value: str, modules: dict[str, str] | None = None) -> str | None:
    """Returns a stable first-party path for workspace and local-module sources."""
    value = value.replace("\\", "/")
    roots = ("xff/", "xff_extras_api/")
    for root in roots:
        if value.startswith(root):
            return value
        marker = f"/{root}"
        if marker in value:
            return root + value.split(marker, 1)[1]

    # Bazel reports local_path_override sources under external/xff_NAME+/. Give
    # each extension a stable, concise namespace independent of the execroot.
    modules = coverage_sources.declared_extras() if modules is None else modules
    for module, directory in modules.items():
        match = re.search(rf"(?:^|/)external/{re.escape(module)}\+/(.*)$", value)
        if match:
            return f"{module}/{match.group(1)}"
        prefix = directory.rstrip("/") + "/"
        if value.startswith(prefix):
            return f"{module}/" + value.removeprefix(prefix)
        marker = f"/{prefix}"
        if marker in value:
            return f"{module}/" + value.split(marker, 1)[1]
    return None


def _source_path(name: str, source_root: Path, modules: dict[str, str]) -> Path:
    for module, directory in modules.items():
        prefix = module + "/"
        if name.startswith(prefix):
            return source_root / directory / name.removeprefix(prefix)
    return source_root / name


def parse_lcov(
    path: Path,
    source_root: Path = Path("."),
    modules: dict[str, str] | None = None,
) -> dict[str, FileCoverage]:
    modules = coverage_sources.declared_extras() if modules is None else modules
    result: dict[str, FileCoverage] = {}
    current: FileCoverage | None = None
    function_lines: dict[str, int] = {}
    records = []
    for record in path.read_text(encoding="utf-8").split("end_of_record\n"):
        match = re.search(r"(?m)^SF:(.+)$", record)
        if not match:
            continue
        name = _repo_path(match.group(1), modules)
        source = _source_path(name, source_root, modules) if name else None
        records.append(
            coverage_sources.normalize_record(record, source)
            if source is not None and source.is_file()
            else record
        )
    for raw in "end_of_record\n".join(records).splitlines():
        if raw.startswith("SF:"):
            name = _repo_path(raw[3:], modules)
            current = result.setdefault(name, FileCoverage()) if name else None
            function_lines = {}
        elif current is not None and raw.startswith("FN:"):
            definition = raw[3:].split(",")
            function_lines[definition[-1]] = int(definition[0])
        elif current is not None and raw.startswith("DA:"):
            line, hits, *_ = raw[3:].split(",")
            current.lines[int(line)] = current.lines.get(int(line), 0) + int(hits)
        elif current is not None and raw.startswith("FNDA:"):
            hits, name = raw[5:].split(",", 1)
            current.functions.append((function_lines.get(name, 0), int(hits)))
        elif current is not None and raw.startswith("BRDA:"):
            line, _, _, taken = raw[5:].split(",")
            current.branches.append((int(line), taken not in ("-", "0")))
        elif raw == "end_of_record":
            current = None
    return result


def _matches(path: str, patterns: list[str]) -> bool:
    return any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)


def select_files(report: dict[str, FileCoverage], policy: dict) -> dict[str, FileCoverage]:
    include = policy.get("include", ["xff/**"])
    exclude = policy.get("exclude", [])
    return {
        path: data
        for path, data in report.items()
        if _matches(path, include) and not _matches(path, exclude)
    }


def counts(files: dict[str, FileCoverage], changed: dict[str, set[int]] | None = None) -> dict:
    values = {"lines": [0, 0], "functions": [0, 0], "branches": [0, 0]}
    for path, data in files.items():
        lines = changed.get(path, set()) if changed is not None else None
        for line, hits in data.lines.items():
            if lines is None or line in lines:
                values["lines"][1] += 1
                values["lines"][0] += hits > 0
        if changed is None:
            values["functions"][1] += len(data.functions)
            values["functions"][0] += sum(hits > 0 for _, hits in data.functions)
        for line, taken in data.branches:
            if lines is None or line in lines:
                values["branches"][1] += 1
                values["branches"][0] += taken
    return {
        metric: {
            "covered": value[0],
            "total": value[1],
            "percent": round(100.0 * value[0] / value[1], 2) if value[1] else None,
        }
        for metric, value in values.items()
    }


def changed_lines(base: str) -> dict[str, set[int]]:
    diff = subprocess.run(
        ["git", "diff", "--unified=0", f"{base}...HEAD", "--", "xff", "xff_extras_api", "extra_modules"],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    ).stdout
    changed: dict[str, set[int]] = defaultdict(set)
    path: str | None = None
    for line in diff.splitlines():
        if line.startswith("+++ b/"):
            path = _repo_path(line[6:])
        elif path and line.startswith("@@"):
            match = re.search(r"\+(\d+)(?:,(\d+))?", line)
            if match:
                start, length = int(match.group(1)), int(match.group(2) or 1)
                changed[path].update(range(start, start + length))
    return changed


def measurements(files: dict[str, FileCoverage], policy: dict) -> dict:
    result = {"overall": counts(files)}
    for group, categories in policy.get("categories", {}).items():
        for name, category in categories.items():
            selected = {path: data for path, data in files.items() if _matches(path, category["include"])}
            result[f"{group} / {name}"] = counts(selected)
    return result


def thresholds(policy: dict) -> tuple[dict, dict]:
    """Returns effective medium and high boundaries for every report row."""
    resolved = coverage_policy.policies(policy)
    minimums = {
        category: {metric: value.minimum for metric, value in values.items()}
        for category, values in resolved.items()
    }
    targets = {
        category: {metric: value.target for metric, value in values.items()}
        for category, values in resolved.items()
    }
    return minimums, targets


def failures(measured: dict, policies: dict) -> list[str]:
    result = []
    for category, values in policies.items():
        for metric, policy in values.items():
            actual = measured[category][metric]["percent"]
            if not coverage_policy.passes(actual, policy):
                boundary = policy.minimum if policy.enforce == "medium" else policy.target
                result.append(
                    f"{category} {metric}: {actual}% is below enforced {policy.enforce} "
                    f"boundary {boundary:g}%"
                )
    return result


def coverage_status(metrics: dict, policy: dict) -> str:
    if not policy:
        return "N/A"
    abbreviations = {"lines": "L", "functions": "F", "branches": "B"}
    problems: dict[str, list[str]] = {"NO DATA": [], "BAD": []}
    for metric in ("lines", "functions", "branches"):
        if metric not in policy:
            continue
        actual = metrics[metric]["percent"]
        if actual is None:
            problems["NO DATA"].append(abbreviations[metric])
        elif not coverage_policy.passes(actual, policy[metric]):
            problems["BAD"].append(abbreviations[metric])
    labels = [f'{name}: {"/".join(values)}' for name, values in problems.items() if values]
    if labels:
        return f'**{"; ".join(labels)}**'
    if all(
        coverage_policy.rating(metrics[metric]["percent"], value) == "high"
        for metric, value in policy.items()
    ):
        return "GOOD"
    return "OK"


def has_coverage(metrics: dict, names: tuple[str, ...]) -> bool:
    return any(metrics[name]["total"] for name in names)


def present_policy(metrics: dict, policy: dict, names: tuple[str, ...]) -> dict:
    """Restricts a policy to metrics for which this measurement has data."""
    return {name: policy[name] for name in names if metrics[name]["total"]}


def markdown(measured: dict, policies: dict) -> str:
    headers = ("Category", "Status", "Lines", "Covered", "Total", "Functions", "Covered", "Total", "Branches", "Covered", "Total")
    values = []
    for category, metrics in measured.items():
        cells = [category, coverage_status(metrics, policies.get(category, {}))]
        for metric in ("lines", "functions", "branches"):
            value = metrics[metric]
            percent = "n/a" if value["percent"] is None else f'{value["percent"]:.2f}%'
            cells.extend((percent, str(value["covered"]), str(value["total"])))
        values.append(tuple(cells))
    widths = tuple(max(len(header), *(len(row[index]) for row in values)) for index, header in enumerate(headers))

    def row(cells: tuple[str, ...]) -> str:
        padded = [cell.ljust(width) for cell, width in zip(cells[:2], widths[:2])]
        padded.extend(cell.rjust(width) for cell, width in zip(cells[2:], widths[2:]))
        return "| " + " | ".join(padded) + " |"

    separators = ("-" * widths[0], "-" * widths[1], *("-" * (width - 1) + ":" for width in widths[2:]))
    return "\n".join([row(headers), row(separators), *(row(value) for value in values)]) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lcov", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--write-baseline", action="store_true")
    parser.add_argument("--base-ref")
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--json-summary", type=Path)
    args = parser.parse_args(argv)
    policy = coverage_sources.resolved_policy(
        json.loads(args.policy.read_text(encoding="utf-8")), coverage_sources.declared_extras()
    )
    files = select_files(parse_lcov(args.lcov), policy)
    measured = measurements(files, policy)
    if not files:
        print("coverage report contains no files selected by policy", file=sys.stderr)
        return 2
    if args.baseline and args.write_baseline:
        baseline = {"schema": 1, "description": "Bazel LCOV with GCC 14; scope and exclusions are defined by coverage_policy.json", "measurements": measured}
        args.baseline.write_text(json.dumps(baseline, indent=2) + "\n", encoding="utf-8")
    effective = coverage_policy.policies(policy)
    minimums, targets = thresholds(policy)
    text = markdown(measured, effective)
    patch_failures = []
    patch = None
    patch_policy = None
    if args.base_ref:
        patch = counts(files, changed_lines(args.base_ref))
        patch_policy = coverage_policy.resolve(policy.get("patch", {}), effective["overall"])
        # A patch can change executable lines without introducing any compiler-reported
        # branches (and vice versa). Enforce only metrics that exist in the patch; absence
        # is not zero coverage.
        patch_policy = present_policy(patch, patch_policy, ("lines", "branches"))
        if has_coverage(patch, ("lines", "branches")):
            text += "\n### Changed coverable lines\n\n" + markdown({"patch": patch}, {"patch": patch_policy})
            patch_failures = failures({"patch": patch}, {"patch": patch_policy})
    print(text, end="")
    if args.summary:
        args.summary.write_text(text, encoding="utf-8")
    if args.json_summary:
        report = {
            "schema": 2,
            "measurements": measured,
            "minimums": minimums,
            "targets": targets,
            "enforcement": {
                category: {metric: value.enforce for metric, value in values.items()}
                for category, values in effective.items()
            },
            "reasons": {
                f"{group} / {name}": category["reason"]
                for group, categories in policy.get("categories", {}).items()
                for name, category in categories.items()
                if category.get("reason")
            },
        }
        if patch is not None and has_coverage(patch, ("lines", "branches")):
            report["patch"] = patch
            assert patch_policy is not None
            report["patch_policy"] = coverage_policy.serializable(patch_policy)
        args.json_summary.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    errors = failures(measured, effective) + patch_failures
    for error in errors:
        print(f"coverage threshold failed: {error}", file=sys.stderr)
    return bool(errors)


if __name__ == "__main__":
    sys.exit(main())
