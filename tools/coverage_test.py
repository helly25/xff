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
            "enforce": "medium",
            "categories": {
                "program": {
                    "matching": {"include": ["xff/matching/**"]},
                    "filesystem": {
                        "include": ["xff/filesystem/**"],
                        "minimum": {"branches": 50},
                        "target": {"branches": 65},
                        "reason": "Filesystem branch onboarding.",
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
            {"lines": 90, "functions": 80, "branches": 65},
            targets["program / filesystem"],
        )
        self.assertEqual(
            {"lines": 90, "functions": 80, "branches": 75},
            targets["program / matching"],
        )

    def test_medium_measurement_passes_medium_enforcement(self):
        metrics = {
            "lines": {"covered": 8, "total": 10, "percent": 80.0},
            "functions": {"covered": 7, "total": 10, "percent": 70.0},
            "branches": {"covered": 6, "total": 10, "percent": 60.0},
        }
        policy = coverage_tool.coverage_policy.resolve(
            {
                "minimum": {"lines": 75, "functions": 60, "branches": 50},
                "target": {"lines": 90, "functions": 90, "branches": 90},
                "enforce": "medium",
            }
        )
        self.assertEqual("OK", coverage_tool.coverage_status(metrics, policy))
        self.assertEqual([], coverage_tool.failures({"area": metrics}, {"area": policy}))

    def test_medium_measurement_fails_high_enforcement(self):
        metrics = {
            metric: {"covered": 9, "total": 10, "percent": 90.0}
            for metric in ("lines", "functions", "branches")
        }
        policy = coverage_tool.coverage_policy.resolve(
            {
                "minimum": {metric: 80 for metric in metrics},
                "target": {metric: 95 for metric in metrics},
                "enforce": "high",
            }
        )
        self.assertEqual("**BAD: L/F/B**", coverage_tool.coverage_status(metrics, policy))
        self.assertEqual(3, len(coverage_tool.failures({"area": metrics}, {"area": policy})))

    def test_changed_lines_only_count_coverable_lines(self):
        files = {
            "xff/a.cc": coverage_tool.FileCoverage(
                lines={10: 1, 11: 0, 12: 0}, branches=[(10, True), (11, False)]
            )
        }
        result = coverage_tool.counts(files, {"xff/a.cc": {10, 11, 99}})
        self.assertEqual({"covered": 1, "total": 2, "percent": 50.0}, result["lines"])
        self.assertEqual({"covered": 1, "total": 2, "percent": 50.0}, result["branches"])

    def test_parse_merges_repeated_template_branches_at_marked_source_lines(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "xff/a.cc"
            source.parent.mkdir(parents=True)
            source.write_text(
                "return lhs || rhs;  // LCOV_MERGE_BR_LINE 2: template instances\n",
                encoding="utf-8",
            )
            report = root / "coverage.lcov"
            report.write_text(
                "SF:xff/a.cc\n"
                "BRDA:1,0,0,2\nBRDA:1,0,1,0\n"
                "BRDA:1,0,2,0\nBRDA:1,0,3,3\nend_of_record\n",
                encoding="utf-8",
            )

            files = coverage_tool.parse_lcov(report, root, {})

            self.assertEqual([(1, True), (1, True)], files["xff/a.cc"].branches)

    def test_parse_rejects_an_invalid_template_branch_merge_width(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "xff/a.cc"
            source.parent.mkdir(parents=True)
            source.write_text("return value;  // LCOV_MERGE_BR_LINE 2\n", encoding="utf-8")
            report = root / "coverage.lcov"
            report.write_text(
                "SF:xff/a.cc\n"
                "BRDA:1,0,0,1\nBRDA:1,0,1,0\nBRDA:1,0,2,0\nend_of_record\n",
                encoding="utf-8",
            )

            with self.assertRaisesRegex(ValueError, "cannot merge 3 branch records"):
                coverage_tool.parse_lcov(report, root, {})

    def test_parse_applies_directives_from_an_extension_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "extra_modules/future/future.cc"
            source.parent.mkdir(parents=True)
            source.write_text("return false;  // LCOV_EXCL_LINE\n", encoding="utf-8")
            report = root / "coverage.lcov"
            report.write_text(
                "SF:external/xff_future+/future.cc\nDA:1,0\nend_of_record\n",
                encoding="utf-8",
            )

            files = coverage_tool.parse_lcov(
                report, root, {"xff_future": "extra_modules/future"}
            )

            self.assertEqual({}, files["xff_future/future.cc"].lines)

    def test_patch_without_coverable_code_is_not_reported(self):
        empty = {
            metric: {"covered": 0, "total": 0, "percent": None}
            for metric in ("lines", "functions", "branches")
        }
        self.assertFalse(coverage_tool.has_coverage(empty, ("lines", "branches")))

    def test_patch_enforces_only_metrics_with_coverable_data(self):
        metrics = {
            "lines": {"covered": 38, "total": 38, "percent": 100.0},
            "functions": {"covered": 0, "total": 0, "percent": None},
            "branches": {"covered": 0, "total": 0, "percent": None},
        }
        policy = {
            "lines": coverage_tool.coverage_policy.MetricPolicy(95, 98, "medium"),
            "branches": coverage_tool.coverage_policy.MetricPolicy(85, 90, "medium"),
        }
        present_policy = coverage_tool.present_policy(metrics, policy, ("lines", "branches"))
        self.assertEqual(["lines"], list(present_policy))
        self.assertEqual([], coverage_tool.failures({"patch": metrics}, {"patch": present_policy}))


if __name__ == "__main__":
    unittest.main()
