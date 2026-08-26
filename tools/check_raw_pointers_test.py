#!/usr/bin/env python3
"""Tests for check_raw_pointers.py."""

from __future__ import annotations

import pathlib
import tempfile
import unittest

import check_raw_pointers


class CheckRawPointersTest(unittest.TestCase):

  def test_finds_object_pointer_declarators(self):
    self.assertEqual(
        check_raw_pointers.pointer_lines(
            """\
const Thing* Lookup();
void Call(struct CApi* handle, const char* text);
int (*callback)(void* data) = nullptr;
"""
        ),
        [1, 2],
    )

  def test_ignores_products_dereferences_comments_and_literals(self):
    self.assertEqual(
        check_raw_pointers.pointer_lines(
            """\
return number * multiplier;
const int value = count * width;
Use(*value);
// const Thing* comment;
constexpr std::string_view text = "const Thing* literal";
"""
        ),
        [],
    )

  def test_requires_a_local_abi_reason(self):
    path = pathlib.Path("sample.h")
    self.assertEqual(
        check_raw_pointers.violations(path, "Thing* missing;\n"),
        ["sample.h:1: raw object pointer needs `XFF_ABI_POINTER: reason`"],
    )
    self.assertEqual(
        check_raw_pointers.violations(path, "// XFF_ABI_POINTER: C callback.\nThing* allowed;\n"),
        [],
    )

  def test_scans_new_extension_directories(self):
    with tempfile.TemporaryDirectory() as temp_dir:
      root = pathlib.Path(temp_dir)
      extension = root / "extra_modules" / "future" / "future.h"
      extension.parent.mkdir(parents=True)
      extension.write_text("Future* Lookup();\n", encoding="utf-8")
      self.assertEqual(list(check_raw_pointers.source_files(root)), [extension])

  def test_implementation_scan_ignores_local_c_api_adapters(self):
    source = """void Run() {
  const char* value = getenv(\"VALUE\");
}
Thing* MakeThing() {
  return nullptr;
}
"""
    self.assertEqual(check_raw_pointers.pointer_lines(source, header=False), [4])


if __name__ == "__main__":
  unittest.main()
