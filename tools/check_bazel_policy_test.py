#!/usr/bin/env python3
"""Tests for check_bazel_policy.py."""

from __future__ import annotations

import pathlib
import tempfile
import unittest

import check_bazel_policy


class CheckBazelPolicyTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tempdir.name)

    def tearDown(self) -> None:
        self.tempdir.cleanup()

    def write_policy(self, *, visibility: str = "private", host: bool = True) -> None:
        (self.root / "BUILD.bazel").write_text(
            f'package(default_visibility = ["//visibility:{visibility}"])\n', encoding="utf-8"
        )
        host_features = " --host_features=layering_check,parse_headers" if host else ""
        (self.root / ".bazelrc").write_text(
            f"common --features=layering_check,parse_headers{host_features}\n", encoding="utf-8"
        )

    def test_accepts_complete_policy(self) -> None:
        self.write_policy()
        self.assertEqual(check_bazel_policy.violations(self.root), [])

    def test_rejects_nonprivate_package(self) -> None:
        self.write_policy(visibility="public")
        self.assertIn("default_visibility", "\n".join(check_bazel_policy.violations(self.root)))

    def test_rejects_missing_host_feature_policy(self) -> None:
        self.write_policy(host=False)
        self.assertIn("--host_features", "\n".join(check_bazel_policy.violations(self.root)))


if __name__ == "__main__":
    unittest.main()
