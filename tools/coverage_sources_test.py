#!/usr/bin/env python3
"""Tests for tools/coverage_sources.py."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import coverage_sources  # noqa: E402


class CoverageSourcesTest(unittest.TestCase):
    def test_maps_every_declared_extra_without_a_hard_coded_module_list(self):
        report = (
            "SF:xff/cli/main.cc\n"
            "SF:external/xff_archive+/archive_fs.cc\n"
            "SF:external/xff_future+/future.cc\n"
        )
        self.assertEqual(
            "SF:xff/cli/main.cc\n"
            "SF:extra_modules/archive/archive_fs.cc\n"
            "SF:extra_modules/future/future.cc\n",
            coverage_sources.remap(
                report,
                {
                    "xff_archive": "extra_modules/archive",
                    "xff_future": "extra_modules/future",
                },
            ),
        )


if __name__ == "__main__":
    unittest.main()
