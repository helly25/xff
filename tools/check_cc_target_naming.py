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
"""Assert every ``cc_library`` target name ends in ``_cc``.

Project convention (``STYLE_CPP.md``): a C++ library target is named ``<thing>_cc``
(``glob_cc``, ``license_cc``, ``vfs_cc``), so a label reads as "the C++ library for
<thing>" and never collides with the directory / file / binary of the same name. Tests
keep their ``_test`` suffix and are not checked here.

The convention was previously unwritten and only enforced by review, which let four
targets drift (``regex_backend``, ``license_notice``, ``archive_reader``,
``pcre2_backend`` - all in the removable extras, exactly where review attention is
thinnest). This check is the enforcement, so a rename cannot silently regress.

Usage:
    check_cc_target_naming.py [FILE...]

With no arguments it walks the repository for ``BUILD.bazel`` / ``BUILD`` files;
pre-commit passes the changed files instead. ``bazel-*`` symlink trees, ``external/``
checkouts and vendored ``third_party/`` modules are skipped (their target names are not
ours to choose). Exits non-zero listing every offender as
``path:line: target -> suggested name``.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# The rule kinds this convention covers. cc_test / cc_binary carry their own suffix
# conventions (`_test`, none) and are deliberately not checked here.
_CHECKED_KINDS = ("cc_library",)

_SUFFIX = "_cc"

# Target names allowed to break the rule, as "<package-relative path>:<name>". Empty by
# design: an exception should be rare enough to argue for in review. Add an entry only
# with a comment saying why the name cannot carry the suffix.
_ALLOWLIST: frozenset[str] = frozenset()

# A rule call opens with `kind(` at the start of a line (buildifier formats top-level
# rules at column 0) and its `name = "..."` attribute follows on some later line. Both
# the multi-line and the rare single-line form are matched.
_RULE_OPEN = re.compile(r"^(\w+)\(")
_NAME_ATTR = re.compile(r'^\s*name\s*=\s*"([^"]*)"')
_INLINE_NAME = re.compile(r'^\w+\(\s*name\s*=\s*"([^"]*)"')

_SKIP_DIRS = ("bazel-", "external", "third_party", ".git")


def _is_skipped(path: Path) -> bool:
    return any(part == skip or part.startswith("bazel-") for part in path.parts for skip in _SKIP_DIRS)


def find_build_files(root: Path) -> list[Path]:
    """Every BUILD.bazel / BUILD file under `root`, minus generated and vendored trees."""
    found = [p for p in root.rglob("BUILD.bazel") if not _is_skipped(p.relative_to(root))]
    found += [p for p in root.rglob("BUILD") if not _is_skipped(p.relative_to(root))]
    return sorted(found)


def violations(path: Path, text: str) -> list[tuple[int, str, str]]:
    """Return (line number, target name, rule kind) for each misnamed library."""
    found: list[tuple[int, str, str]] = []
    kind: str | None = None
    kind_line = 0
    for number, line in enumerate(text.splitlines(), start=1):
        inline = _INLINE_NAME.match(line)
        opened = _RULE_OPEN.match(line)
        if inline and opened:
            # Single-line rule: kind and name on one line, nothing to carry forward.
            if opened.group(1) in _CHECKED_KINDS:
                found.append((number, inline.group(1), opened.group(1)))
            kind = None
            continue
        if opened:
            kind = opened.group(1) if opened.group(1) in _CHECKED_KINDS else None
            kind_line = number
            continue
        if kind is None:
            continue
        name = _NAME_ATTR.match(line)
        if name:
            found.append((kind_line, name.group(1), kind))
            kind = None
    return [(line, name, rule) for line, name, rule in found if not name.endswith(_SUFFIX)]


def _display(path: Path, root: Path) -> Path:
    """`path` relative to `root` when it is inside it, else unchanged.

    An absolute path outside the tree (a caller passing an arbitrary file) must still
    report, so this never raises the way a bare `relative_to` would.
    """
    try:
        return path.relative_to(root)
    except ValueError:
        return path


def check(paths: list[Path], root: Path) -> list[str]:
    """Return one human-readable complaint per offending target."""
    problems: list[str] = []
    for path in paths:
        try:
            text = path.read_text()
        except OSError as error:  # unreadable file: report rather than skip silently
            problems.append(f"{path}: cannot read ({error})")
            continue
        relative = _display(path, root)
        for line, name, kind in violations(path, text):
            if f"{relative.parent}:{name}" in _ALLOWLIST:
                continue
            problems.append(f"{relative}:{line}: {kind} '{name}' should be named '{name}{_SUFFIX}'")
    return problems


def main(argv: list[str]) -> int:
    root = Path.cwd()
    paths = [Path(arg) for arg in argv[1:]] or find_build_files(root)
    problems = check(paths, root)
    if not problems:
        return 0
    print(
        f"cc_library target names must end in '{_SUFFIX}' (STYLE_CPP.md); rename these and their deps:",
        file=sys.stderr,
    )
    for problem in problems:
        print(f"  {problem}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
