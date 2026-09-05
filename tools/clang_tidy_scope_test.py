#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from tools import clang_tidy_scope


class ClangTidyScopeTest(unittest.TestCase):
    def test_source_changes_are_narrow_and_sorted(self):
        database = [{"file": "xff/z.cc"}, {"file": "xff/a.cc"}, {"file": "external/x.cc"}]
        self.assertEqual(
            clang_tidy_scope.select_sources(database, ["xff/z.cc", "xff/a.cc"], {}),
            ["xff/a.cc", "xff/z.cc"],
        )

    def test_header_changes_select_transitive_dependents(self):
        database = [
            {"file": "xff/z.cc"},
            {"file": "xff/a.cc"},
            {"file": "external/x.cc"},
            {"file": "/tmp/absolute.cc"},
        ]
        self.assertEqual(
            clang_tidy_scope.select_sources(
                database,
                ["xff/a.h"],
                {
                    "xff/a.cc": {"xff/a.h"},
                    "xff/middle.h": {"xff/a.h"},
                    "xff/z.cc": {"xff/middle.h"},
                },
            ),
            ["xff/a.cc", "xff/z.cc"],
        )

    def test_unrelated_header_does_not_select_sources(self):
        database = [{"file": "xff/a.cc"}]
        self.assertEqual(clang_tidy_scope.select_sources(database, ["xff/other.h"], {}), [])

    def test_include_graph_resolves_workspace_and_relative_headers(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "xff" / "example"
            package.mkdir(parents=True)
            (package / "detail.h").write_text("", encoding="utf-8")
            (package / "public.h").write_text('#include "detail.h"\n', encoding="utf-8")
            (package / "use.cc").write_text(
                '#include "xff/example/public.h"\n', encoding="utf-8"
            )

            self.assertEqual(
                clang_tidy_scope.include_graph(root),
                {
                    "xff/example/detail.h": set(),
                    "xff/example/public.h": {"xff/example/detail.h"},
                    "xff/example/use.cc": {"xff/example/public.h"},
                },
            )


if __name__ == "__main__":
    unittest.main()
