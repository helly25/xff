#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
# SPDX-License-Identifier: Apache-2.0

"""Generate per-run and retained-site HTML coverage indexes."""

from __future__ import annotations

import argparse
import datetime
import html
import json
import re
from pathlib import Path

import coverage_policy

_METRICS = ("lines", "branches", "functions")


def _page(title: str, body: str) -> str:
    return f"""<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>{html.escape(title)}</title>
    <style>
      body {{ font: 16px/1.5 system-ui, sans-serif; margin: 2rem auto; max-width: 76rem; padding: 0 1rem; }}
      a {{ color: #0969da; }}
      table {{ border-collapse: collapse; margin: 1rem 0 2rem; }}
      th, td {{ border: 1px solid #d0d7de; padding: .35rem .65rem; text-align: right; }}
      .coverageTable th:first-child, .coverageTable td:first-child,
      .reportsTable th:nth-child(-n+6), .reportsTable td:nth-child(-n+6) {{ text-align: left; }}
      .coverageTable td {{ white-space: nowrap; }}
      .coverageTable td:nth-child(n+3), .patchTable td:nth-child(n+2), .reportsTable td:nth-child(n+7) {{ font-family: ui-monospace, SFMono-Regular, Consolas, monospace; font-variant-numeric: tabular-nums; }}
      .low {{ background: #f8cecc; }}
      .medium {{ background: #fff2cc; }}
      .high {{ background: #d5e8d4; }}
      .policy {{ display: block; font: 11px/1.3 system-ui, sans-serif; margin-top: .2rem; white-space: nowrap; }}
      .policyBand {{ border: 1px solid transparent; display: inline-block; padding: 0 .2rem; }}
      .policyBand.enforced {{ border-color: #24292f; font-weight: 700; }}
      .status-good {{ background: #d5e8d4; }}
      .status-ok {{ background: #fff2cc; }}
      .status-bad {{ background: #f8cecc; color: #cf222e; font-weight: bold; }}
    </style>
  </head>
  <body>
{body}
  </body>
</html>
"""


def _percent(value: dict) -> str:
    return "n/a" if value["percent"] is None else f'{value["percent"]:.2f}%'


def _policy(summary: dict, category: str) -> dict[str, coverage_policy.MetricPolicy]:
    return {
        metric: coverage_policy.MetricPolicy(
            summary["minimums"][category][metric],
            summary["targets"][category][metric],
            summary["enforcement"][category][metric],
        )
        for metric in _METRICS
    }


def _status(metrics: dict, policy: dict[str, coverage_policy.MetricPolicy]) -> str:
    names = tuple(name for name in _METRICS if name in policy)
    failures = [name for name in names if not coverage_policy.passes(metrics[name]["percent"], policy[name])]
    if failures:
        return "BAD: " + "/".join(name[0].upper() for name in failures)
    if all(coverage_policy.rating(metrics[name]["percent"], policy[name]) == "high" for name in names):
        return "GOOD"
    return "OK"


def _policy_strip(policy: coverage_policy.MetricPolicy) -> str:
    values = (
        ("low", f"L &lt;{policy.minimum:g}"),
        ("medium", "M &mdash;" if policy.minimum == policy.target else f"M &ge;{policy.minimum:g}"),
        ("high", f"H &ge;{policy.target:g}"),
    )
    spans = []
    for rating, label in values:
        enforced = " enforced" if rating == policy.enforce else ""
        spans.append(f'<span class="policyBand {rating}{enforced}">{label}</span>')
    return '<span class="policy">' + " ".join(spans) + "</span>"


def _status_class(status: str) -> str:
    if status == "GOOD":
        return "status-good"
    if status == "OK":
        return "status-ok"
    return "status-bad"


def _full_table(summary: dict) -> str:
    rows = []
    for category, metrics in summary["measurements"].items():
        policy = _policy(summary, category)
        status = _status(metrics, policy)
        cells: list[tuple[str, str | None]] = [
            (html.escape(category), None),
            (html.escape(status), f'class="{_status_class(status)}"'),
        ]
        for metric in _METRICS:
            value = metrics[metric]
            rating = coverage_policy.rating(value["percent"], policy[metric])
            metric_policy = policy[metric]
            title = (
                f"{rating}; enforce {metric_policy.enforce}; medium at {metric_policy.minimum:g}%; "
                f"high at {metric_policy.target:g}%"
            )
            attrs = f'class="{rating}" title="{html.escape(title, quote=True)}"'
            rate = _percent(value) + _policy_strip(metric_policy)
            counts = f'{value["covered"]} / {value["total"]}'
            cells.extend(((rate, attrs), (counts, None)))
        row = f"      <tr><td>{cells[0][0]}</td><td {cells[1][1]}>{cells[1][0]}</td>"
        row += "".join(
            f'<td{f" {attrs}" if attrs else ""}>{cell}</td>' for cell, attrs in cells[2:]
        ) + "</tr>"
        rows.append(row)
    return """    <table class="coverageTable">
      <thead>
        <tr><th rowspan="2">Category</th><th rowspan="2">Status</th><th colspan="2">Lines</th><th colspan="2">Branches</th><th colspan="2">Functions</th></tr>
        <tr><th>Rate / policy</th><th>Covered / total</th><th>Rate / policy</th><th>Covered / total</th><th>Rate / policy</th><th>Covered / total</th></tr>
      </thead>
      <tbody>
""" + "\n".join(rows) + """
      </tbody>
    </table>"""


def render_report(summary: dict, target: str) -> str:
    """Returns the landing page for one retained report."""
    overview = "../" * len(target.split("/"))
    body = f"    <h1>xff coverage: {html.escape(target)}</h1>\n{_full_table(summary)}\n"
    if "patch" in summary:
        patch = summary["patch"]
        serialized = summary["patch_policy"]
        patch_policy = {
            metric: coverage_policy.MetricPolicy(
                serialized["minimum"][metric],
                serialized["target"][metric],
                serialized["enforce"][metric],
            )
            for metric in ("lines", "branches")
        }
        patch_status = _status(patch, patch_policy)
        values = []
        for metric in ("lines", "branches"):
            rating = coverage_policy.rating(patch[metric]["percent"], patch_policy[metric])
            values.append(
                f'<td class="{rating}">{_percent(patch[metric])}'
                f"{_policy_strip(patch_policy[metric])}</td>"
            )
        status_class = _status_class(patch_status)
        body += (
            "    <h2>Changed coverable lines</h2>\n"
            "    <table class=\"patchTable\"><thead><tr><th>Status</th><th>Lines</th><th>Branches</th></tr></thead><tbody><tr>"
            f'<td class="{status_class}">{patch_status}</td>{"".join(values)}</tr></tbody></table>\n'
        )
    body += (
        '    <p><a href="lcov/">Browse detailed LCOV source coverage</a> · '
        '<a href="coverage-summary.json">Coverage data (JSON)</a> · '
        '<a href="coverage-meta.json">Report metadata (JSON)</a> · '
        f'<a href="{overview}">All reports</a></p>'
    )
    return _page(f"xff coverage: {target}", body)


def _version_key(value: str) -> tuple[int, ...]:
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", value)
    return tuple(map(int, match.groups())) if match else (-1,)


def report_metadata(
    summary: dict,
    target: str,
    created_at: str,
    started_at: str,
    completed_at: str,
    run_id: int,
    run_attempt: int,
    head_sha: str,
) -> dict:
    """Returns the retained identity and overview data for one report."""
    return {
        "schema": 1,
        "target": target,
        "source": {
            "created_at": created_at,
            "started_at": started_at,
            "completed_at": completed_at,
            "run_id": run_id,
            "run_attempt": run_attempt,
            "head_sha": head_sha,
        },
        "coverage": summary["measurements"]["overall"],
    }


def is_newer(candidate: dict, current: dict) -> bool:
    """Whether candidate may replace current, including an idempotent replay."""
    def key(value: dict) -> tuple[str, int, int]:
        source = value["source"]
        return (source["created_at"], source["run_id"], source["run_attempt"])

    return key(candidate) >= key(current)


def latest_metadata(values: list[dict]) -> dict[str, dict]:
    """Returns the newest metadata record for every target."""
    result = {}
    for value in values:
        target = value["target"]
        if target not in result or is_newer(value, result[target]):
            result[target] = value
    return result


def _short_row(metadata: dict) -> str:
    target = metadata["target"]
    if target == "main":
        label = "main"
        source = '<a href="https://github.com/helly25/xff/tree/main">main branch</a>'
    elif target.startswith("tag/"):
        release = target.removeprefix("tag/")
        label = f"release {release}"
        source = (
            f'<a href="https://github.com/helly25/xff/releases/tag/v{html.escape(release)}">'
            f"release v{html.escape(release)}</a>"
        )
    else:
        number = target.removeprefix("pr/")
        label = f"PR {number}"
        source = f'<a href="https://github.com/helly25/xff/pull/{html.escape(number)}">PR #{html.escape(number)}</a>'
    source_metadata = metadata["source"]
    run_id = source_metadata["run_id"]
    if run_id == 0:  # Reports retained before metadata was introduced.
        timestamp = commit = run = "n/a"
    else:
        completed = datetime.datetime.fromisoformat(source_metadata["completed_at"].replace("Z", "+00:00"))
        timestamp = completed.astimezone(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
        sha = source_metadata["head_sha"]
        commit = (
            f'<a href="https://github.com/helly25/xff/commit/{html.escape(sha)}">'
            f"<code>{html.escape(sha[:7])}</code></a>"
        )
        attempt = source_metadata["run_attempt"]
        run = f'<a href="https://github.com/helly25/xff/actions/runs/{run_id}">run {run_id}</a>'
        if attempt > 1:
            run += f" (attempt {attempt})"
    values = [_percent(metadata["coverage"][metric]) for metric in _METRICS]
    report = f'<a href="{target}/">{html.escape(label)}</a>'
    data = f'<a href="{target}/coverage-summary.json">JSON</a>'
    details = (report, data, source, timestamp, commit, run)
    return "        <tr>" + "".join(f"<td>{value}</td>" for value in (*details, *values)) + "</tr>"


def render_site(root: Path) -> str:
    """Returns the overview for all retained reports below root."""
    sources = list((root / "main").glob("coverage-meta.json"))
    sources.extend((root / "tag").glob("*/coverage-meta.json"))
    sources.extend((root / "pr").glob("*/coverage-meta.json"))
    metadata = latest_metadata(
        [json.loads(source.read_text(encoding="utf-8")) for source in sources]
    )
    reports = []
    if "main" in metadata:
        reports.append(metadata["main"])
    reports.extend(
        metadata[target]
        for target in sorted(
            (target for target in metadata if target.startswith("tag/")),
            key=lambda target: _version_key(target.removeprefix("tag/")),
            reverse=True,
        )
    )
    reports.extend(
        metadata[target]
        for target in sorted(
            (target for target in metadata if target.startswith("pr/")),
            key=lambda target: int(target.removeprefix("pr/")),
            reverse=True,
        )
    )
    rows = "\n".join(_short_row(metadata) for metadata in reports)
    body = "    <h1>xff coverage reports</h1>\n"
    if rows:
        body += """    <table class="reportsTable"><thead><tr><th>Report</th><th>Data</th><th>Source</th><th>Completed</th><th>Commit</th><th>Workflow</th><th>Lines</th><th>Branches</th><th>Functions</th></tr></thead>
      <tbody>
""" + rows + """
      </tbody>
    </table>
"""
    if not reports:
        body += "    <p>No coverage reports are available.</p>\n"
    return _page("xff coverage reports", body)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    report = subparsers.add_parser("report")
    report.add_argument("summary", type=Path)
    report.add_argument("target")
    report.add_argument("output", type=Path)
    site = subparsers.add_parser("site")
    site.add_argument("root", type=Path)
    site.add_argument("output", type=Path)
    metadata = subparsers.add_parser("metadata")
    metadata.add_argument("summary", type=Path)
    metadata.add_argument("target")
    metadata.add_argument("output", type=Path)
    metadata.add_argument("--created-at", required=True)
    metadata.add_argument("--started-at", required=True)
    metadata.add_argument("--completed-at", required=True)
    metadata.add_argument("--head-sha", required=True)
    metadata.add_argument("--run-attempt", required=True, type=int)
    metadata.add_argument("--run-id", required=True, type=int)
    newer = subparsers.add_parser("newer")
    newer.add_argument("candidate", type=Path)
    newer.add_argument("current", type=Path)
    args = parser.parse_args()
    if args.command == "report":
        args.output.write_text(render_report(json.loads(args.summary.read_text(encoding="utf-8")), args.target), encoding="utf-8")
    elif args.command == "site":
        args.output.write_text(render_site(args.root), encoding="utf-8")
    elif args.command == "metadata":
        summary = json.loads(args.summary.read_text(encoding="utf-8"))
        value = report_metadata(
            summary,
            args.target,
            args.created_at,
            args.started_at,
            args.completed_at,
            args.run_id,
            args.run_attempt,
            args.head_sha,
        )
        args.output.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    else:
        candidate = json.loads(args.candidate.read_text(encoding="utf-8"))
        current = json.loads(args.current.read_text(encoding="utf-8"))
        return 0 if is_newer(candidate, current) else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
