#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com)
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Unit tests for check_module_versions (run via pre-commit / `python3` directly)."""

from __future__ import annotations

import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_module_versions as cmv  # noqa: E402


def _module(name: str, version: str, deps: dict[str, str] | None = None) -> str:
    lines = [f'module(\n    name = "{name}",\n    version = "{version}",\n)\n']
    for dep_name, dep_version in (deps or {}).items():
        lines.append(f'bazel_dep(name = "{dep_name}", version = "{dep_version}")\n')
    return "".join(lines)


def _version_source(version: str) -> str:
    return f'int main() {{ std::cout << "xff {version}\\n"; }}\n'


class CollectTest(unittest.TestCase):
    def _tree(self, root: Path, *, main="0.0.0", api="0.0.0", pcre="0.0.0", literal="0.0.0"):
        # Root module depends on both extras (intra-repo) plus a third-party dep.
        (root / "MODULE.bazel").write_text(
            _module(
                "helly25_xff",
                main,
                {"abseil-cpp": "20250814.2", "xff_extras_api": api, "xff_pcre2": pcre},
            )
        )
        (root / "xff_extras_api").mkdir()
        (root / "xff_extras_api" / "MODULE.bazel").write_text(_module("xff_extras_api", api))
        (root / "extra_modules" / "pcre2").mkdir(parents=True)
        (root / "extra_modules" / "pcre2" / "MODULE.bazel").write_text(
            _module("xff_pcre2", pcre, {"xff_extras_api": api})
        )
        (root / "xff" / "cli").mkdir(parents=True)
        (root / "xff" / "cli" / "main.cc").write_text(_version_source(literal))

    def test_collects_every_location_and_ignores_third_party(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self._tree(root)
            findings, errors = cmv.collect(root)
            self.assertEqual(errors, [])
            whats = sorted(f.what for f in findings)
            # 3 module versions + 3 intra-repo bazel_deps + the --version literal.
            # abseil-cpp is third-party, so it never appears.
            self.assertEqual(
                whats,
                [
                    "bazel_dep xff_extras_api",
                    "bazel_dep xff_extras_api",
                    "bazel_dep xff_pcre2",
                    "module helly25_xff",
                    "module xff_extras_api",
                    "module xff_pcre2",
                    "xff --version",
                ],
            )
            self.assertTrue(all(f.version == "0.0.0" for f in findings))

    def test_skips_bazel_symlink_trees_and_external(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self._tree(root)
            # A stray MODULE.bazel in a bazel-* tree / external checkout with a wrong
            # version must not be picked up.
            (root / "bazel-out").mkdir()
            (root / "bazel-out" / "MODULE.bazel").write_text(_module("stale", "9.9.9"))
            (root / "external").mkdir()
            (root / "external" / "MODULE.bazel").write_text(_module("dep", "8.8.8"))
            findings, errors = cmv.collect(root)
            self.assertEqual(errors, [])
            self.assertNotIn("9.9.9", {f.version for f in findings})
            self.assertNotIn("8.8.8", {f.version for f in findings})

    def test_reports_missing_version_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "MODULE.bazel").write_text(_module("helly25_xff", "0.0.0"))
            _, errors = cmv.collect(root)
            self.assertTrue(any("main.cc" in e for e in errors))

    def test_reports_missing_version_literal(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "MODULE.bazel").write_text(_module("helly25_xff", "0.0.0"))
            (root / "xff" / "cli").mkdir(parents=True)
            (root / "xff" / "cli" / "main.cc").write_text("int main() { return 0; }\n")
            _, errors = cmv.collect(root)
            self.assertTrue(any("--version literal" in e for e in errors))


def _finding(version: str) -> cmv.Finding:
    return cmv.Finding("MODULE.bazel", "module helly25_xff", version)


class DecideTest(unittest.TestCase):
    def _decide(self, findings, errors=None, expected=None) -> int:
        with redirect_stderr(io.StringIO()):
            return cmv.decide(findings, errors or [], expected)

    def test_all_agree_passes(self):
        self.assertEqual(self._decide([_finding("0.0.0"), _finding("0.0.0")]), 0)

    def test_disagreement_fails(self):
        self.assertEqual(self._decide([_finding("0.0.0"), _finding("0.1.0")]), 1)

    def test_expected_match_passes(self):
        self.assertEqual(self._decide([_finding("0.1.0")], expected="0.1.0"), 0)

    def test_expected_mismatch_fails(self):
        self.assertEqual(self._decide([_finding("0.0.0")], expected="0.1.0"), 1)

    def test_structural_errors_fail(self):
        self.assertEqual(self._decide([_finding("0.0.0")], errors=["boom"]), 1)

    def test_no_findings_fail(self):
        self.assertEqual(self._decide([]), 1)


class RepoTreeTest(unittest.TestCase):
    """The real repository must stay internally consistent."""

    def test_repo_versions_all_agree(self):
        findings, errors = cmv.collect(cmv._repo_root())
        self.assertEqual(errors, [])
        self.assertEqual(len({f.version for f in findings}), 1, [(f.location, f.version) for f in findings])


if __name__ == "__main__":
    unittest.main()
