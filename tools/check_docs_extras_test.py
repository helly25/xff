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
"""Unit tests for check_docs_extras (run via pre-commit / `python3` directly)."""

from __future__ import annotations

import os
import pathlib
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_docs_extras as cde  # noqa: E402

# The repo's real flag list, for the one case that must not use a synthetic fixture: an allowlist entry
# naming a flag that no longer exists exempts nothing and hides the next real one.
_REAL_BUILD = (pathlib.Path(__file__).resolve().parent.parent / "xff" / "BUILD.bazel").read_text()

_BUILD = '''
bool_flag(
    name = "xff_archive",
    build_setting_default = False,
)

bool_flag(
    name = "xff_pcre",
    build_setting_default = False,
)
'''


class DeclaredExtrasTest(unittest.TestCase):
    def test_an_allowlisted_flag_is_not_demanded_of_the_docs_config(self):
        # The allowlist is empty today, so exercise the FILTER rather than a real exemption: a flag
        # named in _NOT_EXTRAS must drop out, which is what lets a future sanitizer-style knob exist
        # without --config=xff_docs having to turn it on.
        build = _BUILD + '\nbool_flag(\n    name = "xff_knob",\n    build_setting_default = False,\n)\n'
        self.assertIn("xff_knob", cde.declared_extras(build))
        original = dict(cde._NOT_EXTRAS)
        cde._NOT_EXTRAS["xff_knob"] = "a test-only knob"
        try:
            self.assertNotIn("xff_knob", cde.declared_extras(build))
        finally:
            cde._NOT_EXTRAS.clear()
            cde._NOT_EXTRAS.update(original)

    def test_every_bool_flag_counts_as_an_extra(self):
        self.assertEqual(cde.declared_extras(_BUILD), ["xff_archive", "xff_pcre"])

    def test_the_allowlist_drops_non_extras_and_records_why(self):
        # Every flag the real allowlist names must exist as a bool_flag; a stale entry silently
        # exempts nothing and hides the next real one.
        for flag in cde._NOT_EXTRAS:
            self.assertIn(flag, cde._BOOL_FLAG.findall(_REAL_BUILD), f"{flag} is allowlisted but gone")


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


if __name__ == "__main__":
    unittest.main()
