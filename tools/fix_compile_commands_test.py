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

"""Tests for tools/fix_compile_commands.py."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fix_compile_commands as fcc  # noqa: E402

# The real shape, including a second override and the field order reversed, since the parser must not
# depend on either.
_MODULE_BAZEL = """
bazel_dep(name = "xff_extras_api", version = "0.0.0")
local_path_override(
    module_name = "xff_extras_api",
    path = "xff_extras_api",
)

bazel_dep(name = "xff_archive", version = "0.0.0")
local_path_override(
    path = "extra_modules/archive",
    module_name = "xff_archive",
)
"""


class LocalModulePathsTest(unittest.TestCase):
    def test_reads_both_field_orders(self):
        self.assertEqual(
            fcc.local_module_paths(_MODULE_BAZEL),
            {"xff_extras_api": "xff_extras_api", "xff_archive": "extra_modules/archive"},
        )

    def test_ignores_a_bazel_dep_without_an_override(self):
        self.assertEqual(fcc.local_module_paths('bazel_dep(name = "re2", version = "1")'), {})

    def test_maps_from_separate_root_and_extras_fragments(self):
        root = fcc.local_module_paths(
            'local_path_override(module_name = "xff_extras_api", path = "xff_extras_api")'
        )
        extras = fcc.local_module_paths(
            'local_path_override(module_name = "xff_archive", path = "extra_modules/archive")'
        )
        root.update(extras)
        self.assertEqual(
            root,
            {"xff_archive": "extra_modules/archive", "xff_extras_api": "xff_extras_api"},
        )


class ToSourcePathTest(unittest.TestCase):
    def setUp(self):
        self.remap = fcc.external_prefix_map(fcc.local_module_paths(_MODULE_BAZEL))

    def test_rewrites_the_bzlmod_canonical_directory(self):
        self.assertEqual(
            fcc.to_source_path("external/xff_archive+/archive_reader.cc", self.remap),
            "extra_modules/archive/archive_reader.cc",
        )

    def test_rewrites_the_legacy_and_bare_spellings(self):
        # bazel has spelled the canonical directory `name~` and plain `name` as well, so all three
        # map rather than silently passing through.
        for external in ("external/xff_archive~/a.cc", "external/xff_archive/a.cc"):
            self.assertEqual(fcc.to_source_path(external, self.remap), "extra_modules/archive/a.cc")

    def test_leaves_other_paths_alone(self):
        for path in (
            "xff/cli/main.cc",
            "external/abseil-cpp+/absl/base/log_severity.cc",
            "bazel-out/darwin_arm64-fastbuild/bin/xff/x.h",
        ):
            self.assertEqual(fcc.to_source_path(path, self.remap), path)


class StripSdkLibcxxTest(unittest.TestCase):
    def test_drops_the_flag_and_its_path_argument(self):
        self.assertEqual(
            fcc.strip_sdk_libcxx(
                [
                    "clang++",
                    "-nostdinc++",
                    "-cxx-isystem",
                    "/Applications/Xcode.app/.../MacOSX.sdk/usr/include/c++/v1",
                    "-isysroot",
                    "/Applications/Xcode.app/.../MacOSX.sdk",
                    "-c",
                    "a.cc",
                ]
            ),
            ["clang++", "-isysroot", "/Applications/Xcode.app/.../MacOSX.sdk", "-c", "a.cc"],
        )

    def test_keeps_a_cxx_isystem_that_is_not_the_sdk(self):
        # Only the SDK libc++ is the mismatch; a hermetic or vendored one must survive.
        arguments = ["clang++", "-cxx-isystem", "/hermetic/include/c++/v1", "-c", "a.cc"]
        self.assertEqual(fcc.strip_sdk_libcxx(arguments), arguments)


class FixEntriesTest(unittest.TestCase):
    def setUp(self):
        self.remap = fcc.external_prefix_map(fcc.local_module_paths(_MODULE_BAZEL))

    def test_rewrites_the_source_in_the_command_too(self):
        # A database entry whose `file` and `arguments` disagree makes the pass pointless: the tooling
        # takes the command, so it would parse the execroot path again.
        entries = [
            {
                "file": "external/xff_archive+/archive_reader.cc",
                "arguments": [
                    "clang++",
                    "-Iexternal/xff_archive+",
                    "-c",
                    "external/xff_archive+/archive_reader.cc",
                ],
            }
        ]
        self.assertEqual(fcc.fix_entries(entries, self.remap, system="Linux"), 1)
        self.assertEqual(entries[0]["file"], "extra_modules/archive/archive_reader.cc")
        self.assertEqual(
            entries[0]["arguments"],
            [
                "clang++",
                # The include path is how the BUILD spells it and must not be rewritten.
                "-Iexternal/xff_archive+",
                "-c",
                "extra_modules/archive/archive_reader.cc",
            ],
        )

    def test_the_macos_pass_is_gated_on_the_system(self):
        def entry():
            return [{"file": "xff/a.cc", "arguments": ["clang++", "-nostdinc++", "-c", "xff/a.cc"]}]

        linux = entry()
        fcc.fix_entries(linux, self.remap, system="Linux")
        self.assertIn("-nostdinc++", linux[0]["arguments"])
        darwin = entry()
        fcc.fix_entries(darwin, self.remap, system="Darwin")
        self.assertNotIn("-nostdinc++", darwin[0]["arguments"])


class LabelToExecrootPathTest(unittest.TestCase):
    def test_main_repo_label_is_a_plain_path(self):
        self.assertEqual(fcc.label_to_execroot_path("@@//xff/engine:run.cc"), "xff/engine/run.cc")

    def test_external_repo_label_gets_the_external_prefix(self):
        self.assertEqual(
            fcc.label_to_execroot_path("@@xff_archive+//:archive_reader.cc"),
            "external/xff_archive+/archive_reader.cc",
        )

    def test_root_package_does_not_double_the_slash(self):
        # The bug this test exists for: naive string surgery yields `external/xff_archive+//file.cc`.
        self.assertNotIn("//", fcc.label_to_execroot_path("@@xff_archive+//:archive_fs.cc"))


class MissingFromDatabaseTest(unittest.TestCase):
    def setUp(self):
        self.remap = fcc.external_prefix_map(fcc.local_module_paths(_MODULE_BAZEL))

    def test_reports_a_source_with_no_entry(self):
        entries = [{"file": "xff/engine/run.cc", "arguments": []}]
        missing = fcc.missing_from_database(entries, ["@@//xff/engine:run.cc", "@@//xff/engine:walk.cc"], self.remap)
        self.assertEqual(missing, ["xff/engine/walk.cc"])

    def test_an_extras_label_matches_its_REWRITTEN_entry(self):
        # The comparison must go through the source-path remap: the label says `external/xff_archive+/`
        # while the database has already been rewritten to `extra_modules/archive/`. Comparing raw
        # spellings reports every extras file as missing when all of them are present.
        entries = [{"file": "extra_modules/archive/archive_reader.cc", "arguments": []}]
        self.assertEqual(fcc.missing_from_database(entries, ["@@xff_archive+//:archive_reader.cc"], self.remap), [])

    def test_leading_dot_slash_in_an_entry_still_matches(self):
        entries = [{"file": "./xff/engine/run.cc", "arguments": []}]
        self.assertEqual(fcc.missing_from_database(entries, ["@@//xff/engine:run.cc"], self.remap), [])

    def test_blank_lines_are_ignored(self):
        entries = [{"file": "xff/engine/run.cc", "arguments": []}]
        self.assertEqual(fcc.missing_from_database(entries, ["", "  ", "@@//xff/engine:run.cc"], self.remap), [])


if __name__ == "__main__":
    unittest.main()
