#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com)
# SPDX-License-Identifier: Apache-2.0

"""Generate per-run and retained-site HTML coverage indexes."""

from __future__ import annotations

import argparse
import html
import json
import re
from pathlib import Path

_METRICS = ("lines", "functions", "branches")


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
      th:first-child, td:first-child, th:nth-child(2), td:nth-child(2) {{ text-align: left; }}
      .fail {{ font-weight: bold; color: #cf222e; }}
    </style>
  </head>
  <body>
{body}
  </body>
</html>
"""


def _percent(value: dict) -> str:
    return "n/a" if value["percent"] is None else f'{value["percent"]:.2f}%'


def _status(metrics: dict, minimums: dict, targets: dict) -> str:
    failures = [name for name in _METRICS if metrics[name]["percent"] is None or metrics[name]["percent"] < minimums.get(name, 0)]
    low = [name for name in _METRICS if name not in failures and metrics[name]["percent"] < targets.get(name, minimums.get(name, 0))]
    if failures:
        return "FAIL: " + "/".join(name[0].upper() for name in failures)
    if low:
        return "LOW: " + "/".join(name[0].upper() for name in low)
    return "OK"


def _full_table(summary: dict) -> str:
    rows = []
    for category, metrics in summary["measurements"].items():
        minimums = summary["minimums"].get(category, {})
        targets = summary["targets"].get(category, {})
        status = _status(metrics, minimums, targets)
        cells = [html.escape(category), html.escape(status)]
        for metric in _METRICS:
            value = metrics[metric]
            cells.extend((_percent(value), str(value["covered"]), str(value["total"])))
        css = ' class="fail"' if status.startswith("FAIL") else ""
        rows.append("      <tr" + css + ">" + "".join(f"<td>{cell}</td>" for cell in cells) + "</tr>")
    return """    <table>
      <thead>
        <tr><th rowspan="2">Category</th><th rowspan="2">Status</th><th colspan="3">Lines</th><th colspan="3">Functions</th><th colspan="3">Branches</th></tr>
        <tr><th>Rate</th><th>Covered</th><th>Total</th><th>Rate</th><th>Covered</th><th>Total</th><th>Rate</th><th>Covered</th><th>Total</th></tr>
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
        body += "    <h2>Changed coverable lines</h2>\n    <table><thead><tr><th>Lines</th><th>Branches</th></tr></thead><tbody><tr>"
        body += f"<td>{_percent(patch['lines'])}</td><td>{_percent(patch['branches'])}</td></tr></tbody></table>\n"
    body += f'    <p><a href="lcov/">Browse detailed LCOV source coverage</a> · <a href="{overview}">All reports</a></p>'
    return _page(f"xff coverage: {target}", body)


def _reports(root: Path, group: str) -> list[str]:
    directory = root / group
    if not directory.is_dir():
        return []
    return [entry.name for entry in directory.iterdir() if (entry / "index.html").is_file()]


def _version_key(value: str) -> tuple[int, ...]:
    match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", value)
    return tuple(map(int, match.groups())) if match else (-1,)


def _summary(root: Path, target: str) -> dict | None:
    source = root / target / "coverage-summary.json"
    return json.loads(source.read_text(encoding="utf-8")) if source.is_file() else None


def _short_row(root: Path, target: str, label: str) -> str:
    summary = _summary(root, target)
    values = ["n/a"] * 3 if summary is None else [_percent(summary["measurements"]["overall"][metric]) for metric in _METRICS]
    return f'        <tr><td><a href="{target}/">{html.escape(label)}</a></td>' + "".join(f"<td>{value}</td>" for value in values) + "</tr>"


def render_site(root: Path) -> str:
    """Returns the overview for all retained reports below root."""
    releases = sorted(_reports(root, "tag"), key=_version_key, reverse=True)
    pull_requests = sorted(_reports(root, "pr"), key=lambda value: int(value), reverse=True)
    reports = []
    if (root / "main" / "index.html").is_file():
        reports.append(("main", "main"))
    reports.extend((f"tag/{release}", f"release {release}") for release in releases)
    reports.extend((f"pr/{number}", f"PR {number}") for number in pull_requests)
    rows = "\n".join(_short_row(root, target, label) for target, label in reports)
    body = "    <h1>xff coverage reports</h1>\n"
    if rows:
        body += """    <table><thead><tr><th>Report</th><th>Lines</th><th>Functions</th><th>Branches</th></tr></thead>
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
    args = parser.parse_args()
    if args.command == "report":
        args.output.write_text(render_report(json.loads(args.summary.read_text(encoding="utf-8")), args.target), encoding="utf-8")
    else:
        args.output.write_text(render_site(args.root), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
