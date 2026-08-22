#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
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
"""Unit tests for build_rules, the BUILD reader shared by the BUILD lints."""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_rules as br  # noqa: E402


class ParseRulesTest(unittest.TestCase):
    def test_kind_name_and_opening_line_are_captured(self):
        text = '# comment\ncc_library(\n    name = "glob_cc",\n    srcs = ["glob.cc"],\n)\n'
        rules = br.parse_rules(text)
        self.assertEqual([(r.kind, r.name, r.line) for r in rules], [("cc_library", "glob_cc", 2)])

    def test_a_single_line_rule_is_captured(self):
        text = 'cc_library(name = "notice_cc", hdrs = ["notice.h"])\n'
        rules = br.parse_rules(text)
        self.assertEqual([(r.kind, r.name, r.line) for r in rules], [("cc_library", "notice_cc", 1)])

    def test_consecutive_rules_do_not_bleed_into_each_other(self):
        text = 'cc_library(\n    name = "a_cc",\n)\n\ncc_test(\n    name = "a_test",\n    deps = [":a_cc"],\n)\n'
        rules = br.parse_rules(text)
        self.assertEqual([(r.kind, r.name) for r in rules], [("cc_library", "a_cc"), ("cc_test", "a_test")])
        self.assertEqual(rules[0].deps, [])
        self.assertEqual(rules[1].deps, [":a_cc"])

    def test_deps_labels_of_every_form_are_collected(self):
        text = (
            'cc_test(\n    name = "t_test",\n    deps = [\n        ":local_cc",\n'
            '        "//xff/glob:glob_cc",\n        "@abseil-cpp//absl/status",\n    ],\n)\n'
        )
        self.assertEqual(
            br.parse_rules(text)[0].deps,
            [":local_cc", "//xff/glob:glob_cc", "@abseil-cpp//absl/status"],
        )

    def test_deps_inside_a_select_are_collected(self):
        # //xff/cli:xff_full builds its deps this way, so the extras must be visible.
        text = (
            'cc_binary(\n    name = "xff_full",\n    deps = [":main_cc"] + select({\n'
            '        "//xff:xff_pcre_enabled": ["@xff_pcre2//:pcre2_backend_cc"],\n'
            '        "//conditions:default": [],\n    }),\n)\n'
        )
        self.assertEqual(
            br.parse_rules(text)[0].deps,
            [":main_cc", "//xff:xff_pcre_enabled", "@xff_pcre2//:pcre2_backend_cc", "//conditions:default"],
        )

    def test_labels_outside_deps_are_not_collected(self):
        # A library named in `data` or `srcs` is not a dependency; counting it would let a
        # test "cover" a library it never links.
        text = (
            'cc_test(\n    name = "t_test",\n    srcs = ["t.cc"],\n    data = ["//xff/cli:xff"],\n'
            '    deps = [":real_cc"],\n    tags = ["manual"],\n)\n'
        )
        self.assertEqual(br.parse_rules(text)[0].deps, [":real_cc"])

    def test_an_attribute_after_deps_ends_the_deps_list(self):
        text = (
            'cc_library(\n    name = "a_cc",\n    deps = [\n        ":b_cc",\n    ],\n'
            '    alwayslink = True,\n    visibility = ["//visibility:public"],\n)\n'
        )
        self.assertEqual(br.parse_rules(text)[0].deps, [":b_cc"])

    def test_a_rule_without_a_name_is_dropped(self):
        # `package(...)`, `load(...)`, `licenses([...])` are not targets.
        text = 'package(\n    default_visibility = ["//visibility:private"],\n)\ncc_library(\n    name = "a_cc",\n)\n'
        self.assertEqual([r.name for r in br.parse_rules(text)], ["a_cc"])

    def test_an_empty_file_yields_no_rules(self):
        self.assertEqual(br.parse_rules(""), [])


class SkipTest(unittest.TestCase):
    def test_generated_external_and_vendored_trees_are_skipped(self):
        for relative in ("bazel-out/x/BUILD.bazel", "bazel-bin/BUILD", "external/dep/BUILD.bazel"):
            self.assertTrue(br.is_skipped(Path(relative)), relative)
        self.assertTrue(br.is_skipped(Path("third_party/vendored/BUILD.bazel")))

    def test_first_party_paths_are_kept(self):
        for relative in ("BUILD.bazel", "xff/glob/BUILD.bazel", "extra_modules/pcre2/BUILD.bazel"):
            self.assertFalse(br.is_skipped(Path(relative)), relative)

    def test_find_build_files_returns_first_party_files_sorted_by_path(self):
        # Both spellings (BUILD.bazel and BUILD) come back in one path-sorted list, not
        # grouped by spelling, so the report order is stable and predictable.
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            for relative in ("BUILD.bazel", "xff/glob/BUILD.bazel", "bazel-out/BUILD.bazel", "pkg/BUILD"):
                target = root / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text("")
            found = [str(p.relative_to(root)) for p in br.find_build_files(root)]
        self.assertEqual(found, ["BUILD.bazel", "pkg/BUILD", "xff/glob/BUILD.bazel"])


class DisplayPathTest(unittest.TestCase):
    def test_a_path_inside_the_root_is_made_relative(self):
        self.assertEqual(br.display_path(Path("/repo/xff/BUILD.bazel"), Path("/repo")), Path("xff/BUILD.bazel"))

    def test_a_path_outside_the_root_is_returned_unchanged(self):
        self.assertEqual(br.display_path(Path("/elsewhere/BUILD.bazel"), Path("/repo")), Path("/elsewhere/BUILD.bazel"))


if __name__ == "__main__":
    unittest.main()
