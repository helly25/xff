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
"""Unit tests for check_docs_extras (run via pre-commit / `python3` directly)."""

from __future__ import annotations

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_docs_extras as cde  # noqa: E402

_BUILD = '''
bool_flag(
    name = "xff_archive",
    build_setting_default = False,
)

bool_flag(
    name = "xff_msan",
    build_setting_default = False,
)

bool_flag(
    name = "xff_pcre",
    build_setting_default = False,
)
'''


class DeclaredExtrasTest(unittest.TestCase):
    def test_every_bool_flag_counts_as_an_extra(self):
        self.assertEqual(cde.declared_extras(_BUILD), ["xff_archive", "xff_pcre"])

    def test_the_allowlist_drops_non_extras_and_records_why(self):
        # xff_msan gates the instrumented libc++ dependency; it is not a documented feature.
        self.assertNotIn("xff_msan", cde.declared_extras(_BUILD))
        self.assertIn("sanitizer", cde._NOT_EXTRAS["xff_msan"])


class EnabledFlagsTest(unittest.TestCase):
    def test_a_directly_enabled_flag_is_found(self):
        rc = "common:xff_docs --//xff:xff_archive=True\n"
        self.assertEqual(cde.enabled_flags(rc, "xff_docs"), {"xff_archive"})

    def test_inheritance_through_config_is_followed(self):
        # This is the case that matters: xff_docs gets PCRE2 only by including xff_full.
        rc = "common:xff_full --//xff:xff_pcre=True\ncommon:xff_docs --config=xff_full\ncommon:xff_docs --//xff:xff_archive=True\n"
        self.assertEqual(cde.enabled_flags(rc, "xff_docs"), {"xff_pcre", "xff_archive"})

    def test_a_bare_flag_means_on(self):
        self.assertEqual(cde.enabled_flags("common:xff_docs --//xff:xff_archive\n", "xff_docs"), {"xff_archive"})

    def test_an_explicitly_disabled_flag_is_not_enabled(self):
        rc = "common:xff_docs --//xff:xff_archive=False\n"
        self.assertEqual(cde.enabled_flags(rc, "xff_docs"), set())

    def test_a_trailing_comment_does_not_break_parsing(self):
        rc = "common:xff_docs --//xff:xff_archive=True  # every extra on\n"
        self.assertEqual(cde.enabled_flags(rc, "xff_docs"), {"xff_archive"})

    def test_other_configs_are_ignored(self):
        rc = "common:asan --//xff:xff_archive=True\ncommon:xff_docs --//xff:xff_pcre=True\n"
        self.assertEqual(cde.enabled_flags(rc, "xff_docs"), {"xff_pcre"})

    def test_a_config_cycle_terminates(self):
        # A self-referential config must not hang the check.
        rc = "common:a --config=b\ncommon:b --config=a\ncommon:b --//xff:xff_pcre=True\n"
        self.assertEqual(cde.enabled_flags(rc, "a"), {"xff_pcre"})


class TheBlanketFlagTest(unittest.TestCase):
    def test_xff_all_counts_as_enabling_every_extra(self):
        # The whole point of --//xff:xff_all: one line in .bazelrc keeps the docs config complete for
        # extras that do not exist yet, so this check has nothing per-extra to maintain.
        self.assertIn("xff_all", cde.enabled_flags("common:xff_docs --//xff:xff_all=True\n", "xff_docs"))

    def test_the_blanket_flag_is_not_itself_an_extra(self):
        # It is a knob, not a feature, so it must not appear in the list of things that must be enabled -
        # otherwise the check would demand that the docs config enable its own switch.
        self.assertNotIn("xff_all", cde.declared_extras('bool_flag(name = "xff_all")'))


if __name__ == "__main__":
    unittest.main()
