#!/usr/bin/env python3
"""Tests for tools/coverage_lcovrc.py."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import coverage_lcovrc


class CoverageLcovrcTest(unittest.TestCase):
    def test_maps_explicit_presentation_bands_to_lcov(self):
        self.assertEqual(
            coverage_lcovrc.render(
                {
                    "minimum": {"lines": 90, "functions": 95, "branches": 80},
                    "target": {"lines": 92, "functions": 95, "branches": 82},
                    "bands": {
                        "medium": {"lines": 90, "branches": 90, "functions": 90},
                        "high": {"lines": 92, "branches": 95, "functions": 95},
                    },
                }
            ),
            """genhtml_line_hi_limit = 92
genhtml_line_med_limit = 90
genhtml_function_hi_limit = 95
genhtml_function_med_limit = 90
genhtml_branch_hi_limit = 95
genhtml_branch_med_limit = 90
""",
        )

    def test_rejects_an_invalid_minimum(self):
        with self.assertRaisesRegex(ValueError, "branch coverage"):
            coverage_lcovrc.render(
                {
                    "minimum": {"lines": 90, "functions": 90, "branches": 101},
                }
            )

    def test_rejects_a_target_below_the_minimum(self):
        with self.assertRaisesRegex(ValueError, "branch coverage"):
            coverage_lcovrc.render(
                {
                    "minimum": {"lines": 90, "functions": 95, "branches": 80},
                    "target": {"branches": 79},
                }
            )

if __name__ == "__main__":
    unittest.main()
