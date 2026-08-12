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

"""Tests for tools/extras.py."""

import os
import re
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extras  # noqa: E402

# The real shape: an extra, a second extra with the fields reversed, and the shared seam module, which
# is a local module but NOT an extra (it lives at the top level, not under extra_modules/).
_MODULE_BAZEL = """
bazel_dep(name = "xff_extras_api", version = "0.0.0")
local_path_override(
    module_name = "xff_extras_api",
    path = "xff_extras_api",
)

bazel_dep(name = "xff_pcre2", version = "0.0.0")
local_path_override(
    module_name = "xff_pcre2",
    path = "extra_modules/pcre2",
)

bazel_dep(name = "xff_archive", version = "0.0.0")
local_path_override(
    path = "extra_modules/archive",
    module_name = "xff_archive",
)
"""


class ExtrasTest(unittest.TestCase):
    def test_finds_the_extras_under_extra_modules(self):
        self.assertEqual(
            extras.extras(_MODULE_BAZEL),
            {"xff_archive": "extra_modules/archive", "xff_pcre2": "extra_modules/pcre2"},
        )

    def test_excludes_the_shared_seam_module(self):
        # xff_extras_api is always built and is not removable, so it is not an extra. Including it would
        # add a wildcard that duplicates coverage `//...` already provides through the core's deps.
        self.assertNotIn("xff_extras_api", extras.extras(_MODULE_BAZEL))

    def test_is_sorted_so_the_derived_command_line_is_stable(self):
        # A CI command that reorders between runs makes cache keys and diffs noisy for no reason.
        self.assertEqual(list(extras.extras(_MODULE_BAZEL)), ["xff_archive", "xff_pcre2"])

    def test_a_stripped_minimal_tree_has_no_extras(self):
        # What the `minimal` CI cell produces: extra_modules/ deleted and the extras' bazel_dep +
        # local_path_override lines stripped from MODULE.bazel. Zero extras is then the CORRECT answer,
        # so neither derivation may treat it as a parse failure - a fail() there broke that job once.
        stripped = re.sub(r"local_path_override\(\s*[^)]*extra_modules/[^)]*\)", "", _MODULE_BAZEL)
        self.assertEqual(extras.extras(stripped), {})

    def test_ignores_a_bazel_dep_without_a_local_override(self):
        self.assertEqual(extras.extras('bazel_dep(name = "re2", version = "1")'), {})
    def test_check_configured_accepts_a_fully_wired_extra(self):
        self.assertEqual(
            extras.check_configured(
                'local_path_override(module_name = "xff_zip", path = "extra_modules/zip")',
                'XFF_EXTRAS = [struct(module = "xff_zip", flag = "xff_zip", target = "@xff_zip//:z")]',
                "common:xff_docs --//xff:xff_zip=True\n",
            ),
            [],
        )

    def test_check_configured_accepts_a_flag_named_unlike_its_module(self):
        # `xff_pcre2` is gated by `--//xff:xff_pcre`: the pairing is what XFF_EXTRAS exists for, so a
        # check that assumed the names match would reject the real repo.
        self.assertEqual(
            extras.check_configured(
                'local_path_override(module_name = "xff_pcre2", path = "extra_modules/pcre2")',
                'XFF_EXTRAS = [struct(module = "xff_pcre2", flag = "xff_pcre", target = "@xff_pcre2//:p")]',
                "common:xff_full --//xff:xff_pcre=True\n",  # xff_docs inherits xff_full
            ),
            [],
        )

    def test_check_configured_reports_an_extra_missing_from_the_list(self):
        complaints = extras.check_configured(
            'local_path_override(module_name = "xff_zip", path = "extra_modules/zip")',
            "XFF_EXTRAS = []",
            "",
        )
        self.assertEqual(len(complaints), 1)
        self.assertIn("missing from XFF_EXTRAS", complaints[0])

    def test_check_configured_reports_an_extra_absent_from_the_docs_config(self):
        # The failure this hook exists for: the extra builds and links, but --config=xff_docs does not
        # enable it, so the committed reference documents less than the tool does.
        complaints = extras.check_configured(
            'local_path_override(module_name = "xff_zip", path = "extra_modules/zip")',
            'XFF_EXTRAS = [struct(module = "xff_zip", flag = "xff_zip", target = "@xff_zip//:z")]',
            "common:xff_docs --config=xff_full\n",
        )
        self.assertEqual(len(complaints), 1)
        self.assertIn("--config=xff_docs", complaints[0])

    def test_check_configured_reports_a_list_entry_with_no_module(self):
        complaints = extras.check_configured(
            "",
            'XFF_EXTRAS = [struct(module = "xff_gone", flag = "xff_gone", target = "@xff_gone//:g")]',
            "common:xff_docs --//xff:xff_gone=True\n",
        )
        self.assertEqual(len(complaints), 1)
        self.assertIn("MODULE.bazel does not declare", complaints[0])



if __name__ == "__main__":
    unittest.main()
