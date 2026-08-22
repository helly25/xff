#!/usr/bin/env python3
"""Tests for tools/coverage_sources.py."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import coverage_sources  # noqa: E402


class CoverageSourcesTest(unittest.TestCase):
    def test_policy_discovers_a_future_extra_without_a_policy_edit(self):
        policy = {"include": ["xff/**"], "categories": {"extensions": {}}}
        actual = coverage_sources.resolved_policy(
            policy, {"xff_future": "extra_modules/future"}
        )
        self.assertIn("xff_future/**", actual["include"])
        self.assertEqual(
            {"include": ["xff_future/**"]},
            actual["categories"]["extensions"]["Future"],
        )
        self.assertNotIn("xff_future/**", policy["include"])

    def test_maps_every_extra_in_the_repository_registry(self):
        modules = coverage_sources.declared_extras()
        report = "".join(f"SF:external/{module}+/source.cc\n" for module in modules)
        expected = "".join(f"SF:{path}/source.cc\n" for path in modules.values())
        self.assertEqual(expected, coverage_sources.remap(report, modules))

    def test_maps_a_future_extra_without_a_mapper_change(self):
        report = (
            "SF:xff/cli/main.cc\n"
            "SF:external/xff_future+/future.cc\n"
        )
        self.assertEqual(
            "SF:xff/cli/main.cc\n"
            "SF:extra_modules/future/future.cc\n",
            coverage_sources.remap(
                report,
                {
                    "xff_future": "extra_modules/future",
                },
            ),
        )

    def test_groups_sources_by_the_policy_module(self):
        policy = {
            "include": ["xff/**", "xff_archive/**"],
            "exclude": ["**/*_test.cc"],
            "categories": {
                "program": {"command line": {"include": ["xff/cli/**"]}},
                "extensions": {"archive": {"include": ["xff_archive/**"]}},
            },
        }
        report = (
            "SF:xff/cli/main.cc\nDA:1,1\nend_of_record\n"
            "SF:extra_modules/archive/archive_fs.cc\nDA:1,1\nend_of_record\n"
            "SF:xff/cli/main_test.cc\nDA:1,1\nend_of_record\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            actual = coverage_sources.grouped(
                report, {"xff_archive": "extra_modules/archive"}, policy, root
            )
            self.assertIn(f"SF:{root}/program-command-line/xff/cli/main.cc\n", actual)
            self.assertIn(
                f"SF:{root}/extensions-archive/xff_archive/archive_fs.cc\n", actual
            )
            self.assertNotIn("main_test.cc", actual)
            self.assertTrue((root / "program-command-line/xff/cli/main.cc").is_symlink())

    def test_rejects_a_source_without_exactly_one_policy_module(self):
        policy = {
            "include": ["xff/**"],
            "categories": {
                "one": {"first": {"include": ["xff/**"]}},
                "two": {"second": {"include": ["xff/cli/**"]}},
            },
        }
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "belongs to 2 policy categories"):
                coverage_sources.grouped(
                    "SF:xff/cli/main.cc\nend_of_record\n", {}, policy, Path(directory)
                )


if __name__ == "__main__":
    unittest.main()
