#!/usr/bin/env python3
"""Tests for tools/coverage_index.py."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import coverage_index  # noqa: E402


def _summary(percent: float) -> dict:
    metric = {"covered": 9, "total": 10, "percent": percent}
    return {
        "measurements": {"overall": {name: metric for name in ("lines", "functions", "branches")}},
        "minimums": {"overall": {name: 80 for name in ("lines", "functions", "branches")}},
        "targets": {"overall": {name: 90 for name in ("lines", "functions", "branches")}},
    }


class CoverageIndexTest(unittest.TestCase):
    def test_report_contains_policy_table_and_lcov_link(self):
        rendered = coverage_index.render_report(_summary(95.0), "pr/42")
        self.assertIn("xff coverage: pr/42", rendered)
        self.assertIn("95.00%", rendered)
        self.assertIn('href="lcov/"', rendered)
        self.assertIn('href="../../"', rendered)

    def test_site_shows_all_metrics_with_main_first_and_numeric_sorting(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for report in ("main", "pr/9", "pr/42", "tag/0.9.0", "tag/0.10.0"):
                target = root / report
                target.mkdir(parents=True)
                (target / "index.html").touch()
                (target / "coverage-summary.json").write_text(json.dumps(_summary(95.0)))

            rendered = coverage_index.render_site(root)

            self.assertIn("PR 42", rendered)
            self.assertIn("PR 9", rendered)
            self.assertIn("release 0.10.0", rendered)
            self.assertIn("release 0.9.0", rendered)
            self.assertLess(rendered.index('href="main/"'), rendered.index('href="tag/0.10.0/"'))
            self.assertLess(rendered.index('href="tag/0.10.0/"'), rendered.index('href="tag/0.9.0/"'))
            self.assertLess(rendered.index('href="tag/0.9.0/"'), rendered.index('href="pr/42/"'))
            self.assertLess(rendered.index('href="pr/42/"'), rendered.index('href="pr/9/"'))
            self.assertNotIn("<ul>", rendered)

    def test_empty_site_says_no_reports_are_available(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertIn("No coverage reports are available", coverage_index.render_site(Path(directory)))


if __name__ == "__main__":
    unittest.main()
