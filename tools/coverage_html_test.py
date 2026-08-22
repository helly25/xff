#!/usr/bin/env python3
"""Tests for tools/coverage_html.py."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import coverage_html


class CoverageHtmlTest(unittest.TestCase):
    def test_adds_each_metric_policy_to_every_html_page(self):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory)
            page = report / "index.html"
            page.write_text(f"before\n{coverage_html._ANCHOR}\nafter\n", encoding="utf-8")
            coverage_html.apply(
                report,
                {
                    "minimum": {"lines": 90, "functions": 95, "branches": 80},
                    "target": {"lines": 92, "functions": 97, "branches": 82},
                },
            )
            rendered = page.read_text(encoding="utf-8")
            self.assertIn("Coverage policy:", rendered)
            self.assertIn("<b>Lines:</b>", rendered)
            self.assertIn("low: &lt; 90 %", rendered)
            self.assertIn("medium: &gt;= 90 %", rendered)
            self.assertIn("high: &gt;= 92 %", rendered)
            self.assertIn("medium: &gt;= 95 %", rendered)
            self.assertIn("high: &gt;= 97 %", rendered)
            self.assertIn("<b>Branches:</b>", rendered)
            self.assertIn("medium: &gt;= 80 %", rendered)
            self.assertIn("high: &gt;= 82 %", rendered)

    def test_rejects_an_unrecognized_genhtml_page(self):
        with tempfile.TemporaryDirectory() as directory:
            page = Path(directory) / "index.html"
            page.write_text("not a genhtml page", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "ruler anchor not found below"):
                coverage_html.apply(
                    Path(directory),
                    {"minimum": {"lines": 90, "functions": 95, "branches": 80}},
                )


if __name__ == "__main__":
    unittest.main()
