#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
# SPDX-License-Identifier: Apache-2.0
"""Tests for check_namespace_style.py."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import check_namespace_style


class CheckNamespaceStyleTest(unittest.TestCase):
    def check(self, source: str) -> list[str]:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "subject.cc"
            path.write_text(source, encoding="utf-8")
            return check_namespace_style.check(path)

    def test_rejects_global_nested_and_aliased_std_namespaces(self) -> None:
        for source in (
            "namespace std {}\n",
            "namespace xff::std {}\n",
            "namespace std = vendor_std;\n",
            "using namespace std;\n",
            "using namespace ::std;\n",
        ):
            with self.subTest(source=source):
                self.assertTrue(self.check(source))

    def test_accepts_qualified_uses_aliases_and_sanctioned_specialization_shape(self) -> None:
        self.assertFalse(
            self.check(
                """
namespace fs = ::std::filesystem;
using ::std::string;
template <> struct std::hash<MyType> {};
"""
            )
        )

    def test_rejects_relative_global_using_declarations_and_namespace_aliases(self) -> None:
        for source in (
            "using std::string;\n",
            "using absl::Status;\n",
            "using mbo::testing::IsOk;\n",
            "using testing::Eq;\n",
            "using xff::registry::Style;\n",
            "namespace fs = std::filesystem;\n",
        ):
            with self.subTest(source=source):
                self.assertTrue(self.check(source))

    def test_accepts_dependent_imports_and_type_aliases(self) -> None:
        self.assertFalse(
            self.check(
                """
using Ts::operator()...;
using Value = mbo::types::Value;
"""
            )
        )

    def test_rejects_blank_line_within_global_alias_block(self) -> None:
        self.assertTrue(
            self.check(
                """
namespace fs = ::std::filesystem;

using ::mbo::testing::IsOk;
"""
            )
        )

    def test_accepts_one_combined_global_alias_block(self) -> None:
        self.assertFalse(
            self.check(
                """
namespace fs = ::std::filesystem;
using ::mbo::testing::IsOk;
using ::testing::Eq;
"""
            )
        )

    def test_ignores_comments_and_literals(self) -> None:
        self.assertFalse(
            self.check(
                """
// namespace std {}
constexpr auto text = "using namespace std;";
/* namespace xff::std {} */
"""
            )
        )


if __name__ == "__main__":
    unittest.main()
