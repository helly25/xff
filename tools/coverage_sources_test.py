#!/usr/bin/env python3
"""Tests for tools/coverage_sources.py."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import coverage_sources  # noqa: E402


class CoverageSourcesTest(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
