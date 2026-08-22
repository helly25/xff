#!/usr/bin/env python3
"""Tests for tools/coverage_lcovrc.py."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import coverage_lcovrc


class CoverageLcovrcTest(unittest.TestCase):
    def test_maps_policy_minimums_to_metric_specific_color_bands(self):
        self.assertEqual(
            coverage_lcovrc.render(
                {"minimum": {"lines": 90, "functions": 90, "branches": 75}}
            ),
            """genhtml_line_hi_limit = 90
genhtml_line_med_limit = 75
genhtml_function_hi_limit = 90
genhtml_function_med_limit = 75
genhtml_branch_hi_limit = 75
genhtml_branch_med_limit = 60
""",
        )

    def test_rejects_an_invalid_minimum(self):
        with self.assertRaisesRegex(ValueError, "branch coverage"):
            coverage_lcovrc.render(
                {"minimum": {"lines": 90, "functions": 90, "branches": 101}}
            )


if __name__ == "__main__":
    unittest.main()
