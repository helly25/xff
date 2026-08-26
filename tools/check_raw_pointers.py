#!/usr/bin/env python3
"""Rejects unannotated raw object-pointer declarators in xff-owned C++."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from collections.abc import Iterable


ANNOTATION = "XFF_ABI_POINTER:"
SOURCE_ROOTS = ("xff", "xff_extras_api", "extra_modules")
SOURCE_SUFFIXES = frozenset({".cc", ".h"})

_TYPE = r"(?:(?:const|volatile)\s+)*(?:(?:struct|class)\s+)?(?:::)?[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*(?:\s*<[^;{}()]*>)?"
_OBJECT_POINTER = re.compile(rf"(?P<type>{_TYPE})\s*\*\s*(?:(?:const|volatile)\s+)*(?P<name>[A-Za-z_]\w*|\*)")
_TYPE_WORDS = frozenset(
    {
        "auto",
        "bool",
        "char",
        "double",
        "float",
        "int",
        "long",
        "short",
        "signed",
        "unsigned",
        "void",
        "wchar_t",
    }
)


def _without_literals_and_comments(text: str) -> str:
  """Masks comments and literals while preserving line/column positions."""
  out = list(text)
  index = 0
  state = "code"
  quote = ""
  while index < len(text):
    char = text[index]
    nxt = text[index + 1] if index + 1 < len(text) else ""
    if state == "code":
      if char == "/" and nxt == "/":
        out[index] = out[index + 1] = " "
        index += 2
        state = "line_comment"
        continue
      if char == "/" and nxt == "*":
        out[index] = out[index + 1] = " "
        index += 2
        state = "block_comment"
        continue
      if char in {'"', "'"}:
        quote = char
        out[index] = " "
        index += 1
        state = "literal"
        continue
    elif state == "line_comment":
      if char == "\n":
        state = "code"
      else:
        out[index] = " "
      index += 1
      continue
    elif state == "block_comment":
      if char == "*" and nxt == "/":
        out[index] = out[index + 1] = " "
        index += 2
        state = "code"
        continue
      if char != "\n":
        out[index] = " "
      index += 1
      continue
    else:
      if char == "\\" and nxt:
        out[index] = " "
        if nxt != "\n":
          out[index + 1] = " "
        index += 2
        continue
      out[index] = "\n" if char == "\n" else " "
      if char == quote:
        state = "code"
      index += 1
      continue
    index += 1
  return "".join(out)


def _looks_like_type(spelling: str) -> bool:
  words = spelling.replace("::", " ").replace("<", " ").split()
  leaf = words[-1] if words else ""
  return (
      ("::" in spelling and not spelling.lstrip().startswith("struct ::"))
      or "class " in spelling
      or (leaf in _TYPE_WORDS and leaf not in {"char", "void"})
      or (leaf and leaf[0].isupper())
  )


def _function_body_lines(masked: str) -> set[int]:
  """Finds lines inside function bodies with a lightweight balanced-brace scan."""
  result: set[int] = set()
  stack: list[bool] = []
  statement = ""
  line_number = 1
  at_line_start = True
  for char in masked:
    if at_line_start and any(stack):
      result.add(line_number)
    at_line_start = False
    if char == "\n":
      line_number += 1
      at_line_start = True
    if char == "{":
      prefix = statement.strip()
      opens_function = ")" in prefix and not re.match(r"^(?:if|for|while|switch|catch)\b", prefix)
      stack.append(opens_function or any(stack))
      statement = ""
    elif char == "}":
      if stack:
        stack.pop()
      statement = ""
    elif char == ";":
      statement = ""
    else:
      statement += char
  return result


def pointer_lines(text: str, *, header: bool = True) -> list[int]:
  """Returns one-based lines containing likely raw pointers in owned interfaces."""
  masked = _without_literals_and_comments(text)
  source_lines = masked.splitlines()
  function_body_lines = _function_body_lines(masked) if not header else set()
  result: set[int] = set()
  for line_number, line in enumerate(source_lines, 1):
    # Headers declare public and private interfaces. In implementation files only function
    # signatures are interfaces; local values adapting C/POSIX APIs are deliberately not.
    if line_number in function_body_lines:
      continue
    for match in _OBJECT_POINTER.finditer(line):
      if not _looks_like_type(match.group("type")):
        continue
      prefix = line[: match.start()].strip()
      # A product in an initializer/call can look like `Type * name`; the declaration's type is
      # always before the first assignment on its physical line, and `return` starts an expression.
      if "=" in prefix or re.search(r"\breturn\s*$", prefix):
        continue
      result.add(line_number)
  return sorted(result)


def violations(path: pathlib.Path, text: str) -> list[str]:
  lines = text.splitlines()
  failures = []
  for line_number in pointer_lines(text, header=path.suffix == ".h"):
    line = lines[line_number - 1]
    previous = lines[line_number - 2] if line_number > 1 else ""
    if ANNOTATION not in line and ANNOTATION not in previous:
      failures.append(f"{path}:{line_number}: raw object pointer needs `{ANNOTATION} reason`")
  return failures


def source_files(root: pathlib.Path) -> Iterable[pathlib.Path]:
  for source_root in SOURCE_ROOTS:
    directory = root / source_root
    if directory.is_dir():
      yield from (path for path in directory.rglob("*") if path.suffix in SOURCE_SUFFIXES)


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("paths", nargs="*", type=pathlib.Path)
  args = parser.parse_args()
  root = pathlib.Path(__file__).resolve().parents[1]
  paths = args.paths or list(source_files(root))
  failures = []
  for path in paths:
    resolved = path if path.is_absolute() else root / path
    if resolved.suffix in SOURCE_SUFFIXES and resolved.is_file():
      failures.extend(violations(path, resolved.read_text(encoding="utf-8")))
  if failures:
    print("\n".join(failures), file=sys.stderr)
    return 1
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
