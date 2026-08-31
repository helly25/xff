#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

import unittest

from tools import clang_tidy_scope


class ClangTidyScopeTest(unittest.TestCase):
    def test_source_changes_are_narrow_and_sorted(self):
        database = [{"file": "xff/z.cc"}, {"file": "xff/a.cc"}, {"file": "external/x.cc"}]
        self.assertEqual(
            clang_tidy_scope.select_sources(database, ["xff/z.cc", "xff/a.cc"]),
            ["xff/a.cc", "xff/z.cc"],
        )

    def test_header_changes_promote_to_all_first_party_sources(self):
        database = [
            {"file": "xff/z.cc"},
            {"file": "xff/a.cc"},
            {"file": "external/x.cc"},
            {"file": "/tmp/absolute.cc"},
        ]
        self.assertEqual(
            clang_tidy_scope.select_sources(database, ["xff/a.h"]),
            ["xff/a.cc", "xff/z.cc"],
        )

    def test_bzl_changes_promote_to_all_first_party_sources(self):
        database = [{"file": "xff/a.cc"}]
        self.assertEqual(clang_tidy_scope.select_sources(database, ["xff/BUILD.bazel"]), ["xff/a.cc"])


if __name__ == "__main__":
    unittest.main()
