#!/usr/bin/env python3
"""Tests for tools/coverage.py."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import coverage as coverage_tool  # noqa: E402


class CoverageTest(unittest.TestCase):
    def test_normalizes_workspace_and_extension_paths(self):
        cases = {
            "/workspace/xff/cli/main.cc": "xff/cli/main.cc",
            "/execroot/external/xff_archive+/archive_reader.cc": "xff_archive/archive_reader.cc",
            "/checkout/extra_modules/fuse/fuse_server.cc": "xff_fuse/fuse_server.cc",
            "extra_modules/pcre2/pcre2_backend.cc": "xff_pcre2/pcre2_backend.cc",
            "/workspace/third_party/lib.cc": None,
        }
        for source, expected in cases.items():
            with self.subTest(source=source):
                self.assertEqual(expected, coverage_tool._repo_path(source))

    def test_normalizes_a_future_extension_from_the_registry_mapping(self):
        modules = {"xff_future": "extra_modules/future"}
        self.assertEqual(
            "xff_future/reader.cc",
            coverage_tool._repo_path("/execroot/external/xff_future+/reader.cc", modules),
        )
        self.assertEqual(
            "xff_future/reader.cc",
            coverage_tool._repo_path("/checkout/extra_modules/future/reader.cc", modules),
        )

    def test_parse_filter_and_measure(self):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / "coverage.lcov"
            report.write_text(
                "SF:/workspace/xff/matching/regex/regex.cc\n"
                "FNDA:1,Good\nFNDA:0,Bad\n"
                "DA:10,1\nDA:11,0\n"
                "BRDA:10,0,0,1\nBRDA:10,0,1,0\nend_of_record\n"
                "SF:/workspace/xff/matching/regex/regex_test.cc\nDA:1,1\nend_of_record\n",
                encoding="utf-8",
            )
            files = coverage_tool.select_files(
                coverage_tool.parse_lcov(report),
                {"include": ["xff/**"], "exclude": ["**/*_test.cc"]},
            )
            self.assertEqual(["xff/matching/regex/regex.cc"], list(files))
            self.assertEqual(
                {"covered": 1, "total": 2, "percent": 50.0},
                coverage_tool.counts(files)["lines"],
            )

    def test_category_inherits_overall_floor_and_target(self):
        policy = {
            "minimum": {"lines": 90, "functions": 80, "branches": 70},
            "target": {"branches": 75},
            "categories": {
                "program": {
                    "matching": {"include": ["xff/matching/**"]},
                    "filesystem": {
                        "include": ["xff/filesystem/**"],
                        "minimum": {"branches": 50},
                        "target": {"branches": 65},
                    },
                }
            },
        }
        minimums, targets = coverage_tool.thresholds(policy)
        self.assertEqual(policy["minimum"], minimums["program / matching"])
        self.assertEqual(
            {"lines": 90, "functions": 80, "branches": 50},
            minimums["program / filesystem"],
        )
        self.assertEqual(
            {"lines": 90, "functions": 80, "branches": 75},
            targets["program / filesystem"],
        )
        self.assertEqual(
            {"lines": 90, "functions": 80, "branches": 75},
            targets["program / matching"],
        )

    def test_passing_measurement_below_target_is_medium_not_failed(self):
        metrics = {
            "lines": {"covered": 8, "total": 10, "percent": 80.0},
            "functions": {"covered": 7, "total": 10, "percent": 70.0},
            "branches": {"covered": 6, "total": 10, "percent": 60.0},
        }
        floor = {"lines": 75, "functions": 60, "branches": 50}
        target = {"lines": 90, "functions": 80, "branches": 70}
        self.assertEqual("**MEDIUM: L/F/B**", coverage_tool.coverage_status(metrics, floor, target))
        self.assertEqual([], coverage_tool.failures({"area": metrics}, {"area": floor}))

    def test_changed_lines_only_count_coverable_lines(self):
        files = {
            "xff/a.cc": coverage_tool.FileCoverage(
                lines={10: 1, 11: 0, 12: 0}, branches=[(10, True), (11, False)]
            )
        }
        result = coverage_tool.counts(files, {"xff/a.cc": {10, 11, 99}})
        self.assertEqual({"covered": 1, "total": 2, "percent": 50.0}, result["lines"])
        self.assertEqual({"covered": 1, "total": 2, "percent": 50.0}, result["branches"])

    def test_patch_without_coverable_code_is_not_reported(self):
        empty = {
            metric: {"covered": 0, "total": 0, "percent": None}
            for metric in ("lines", "functions", "branches")
        }
        self.assertFalse(coverage_tool.has_coverage(empty, ("lines", "branches")))


if __name__ == "__main__":
    unittest.main()
