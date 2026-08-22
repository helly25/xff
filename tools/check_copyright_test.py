#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
# SPDX-License-Identifier: Apache-2.0
"""Tests for check_copyright.py."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import check_copyright


class CheckCopyrightTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        (self.root / "LICENSE").write_text(
            f"license body\n\n{check_copyright.LICENSE_COPYRIGHT}\n", encoding="utf-8"
        )

    def tearDown(self) -> None:
        self.directory.cleanup()

    def test_accepts_canonical_headers_in_supported_comment_styles(self) -> None:
        marker = "SPDX-" "FileCopyrightText"
        (self.root / "valid.txt").write_text(
            "\n".join(
                (
                    f"# {marker}: {check_copyright.COPYRIGHT}",
                    f"// {marker}: {check_copyright.COPYRIGHT}",
                    f"<!-- {marker}: {check_copyright.COPYRIGHT} -->",
                )
            ),
            encoding="utf-8",
        )

        self.assertEqual(check_copyright.check(self.root, [Path("valid.txt")]), [])

    def test_reports_every_noncanonical_header_and_license(self) -> None:
        marker = "SPDX-" "FileCopyrightText"
        (self.root / "bad.txt").write_text(
            f"# {marker}: Copyright (cfg) Someone Else\n", encoding="utf-8"
        )
        (self.root / "LICENSE").write_text("Copyright [yyyy] [owner]\n", encoding="utf-8")

        self.assertEqual(
            check_copyright.check(self.root, [Path("bad.txt")]),
            [
                "bad.txt:1: copyright is 'Copyright (cfg) Someone Else'; expected "
                f"'{check_copyright.COPYRIGHT}'",
                "LICENSE: copyright declarations are ['Copyright [yyyy] [owner]']; expected "
                f"['{check_copyright.LICENSE_COPYRIGHT}']",
            ],
        )

    def test_ignores_binary_files_and_files_without_a_copyright_header(self) -> None:
        (self.root / "binary").write_bytes(b"\xff\xfe")
        (self.root / "plain.txt").write_text("No SPDX copyright declaration.\n", encoding="utf-8")

        self.assertEqual(
            check_copyright.check(self.root, [Path("binary"), Path("plain.txt")]), []
        )


if __name__ == "__main__":
    unittest.main()
