#!/usr/bin/env python3.13
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
"""Assert that every in-repo version string agrees on one shared version.

The git tag is the version source of truth: the repository stays at the ``0.0.0``
sentinel and the release workflow stamps the tag's version into every location at
build time (see ``docs/`` / the release workflow). This check keeps those locations
from drifting, so the release's stamp always has one uniform value to replace and no
component silently ships a stale version.

The version is bound across all in-repo modules (``helly25_xff`` and every extra such
as ``xff_extras_api`` / ``xff_pcre2``): the extras are not published independently, so
they carry the tool's version rather than their own. The locations checked:

* every in-repo ``MODULE.bazel``'s ``module(version = "...")``;
* every ``bazel_dep(name = <one of our modules>, version = "...")`` (intra-repo deps);
* the ``xff --version`` literal in ``xff/cli/main.cc`` (what the binary reports).

``bazel-*`` symlink trees and ``external/`` checkouts are skipped, and third-party
``bazel_dep``s (abseil, rules_cc, helly25_mbo, ...) are left alone.

Usage:
    check_module_versions.py [EXPECTED]

With no argument the versions only have to agree with each other (the ``0.0.0``
sentinel in a normal checkout). Passing ``EXPECTED`` (e.g. the release tag's version)
additionally asserts they all equal that value - the release flow uses this to verify
the stamp matched the tag. Exits non-zero on any disagreement, listing every location
and its version.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# The module() call spans lines; name and version may appear in either order and with
# other attributes (compatibility_level, ...) interleaved, so match the call body once
# and pull each attribute out of it separately.
_MODULE_CALL = re.compile(r"\bmodule\(([^)]*)\)", re.DOTALL)
_BAZEL_DEP = re.compile(r"\bbazel_dep\(([^)]*)\)", re.DOTALL)
_NAME_ATTR = re.compile(r'\bname\s*=\s*"([^"]*)"')
_VERSION_ATTR = re.compile(r'\bversion\s*=\s*"([^"]*)"')
# The --version literal in main.cc, e.g. `std::cout << "xff 0.0.0\n";`.
_VERSION_LITERAL = re.compile(r'"xff ([^"\\]+)\\n"')

# Location of the binary's --version literal, relative to the repo root.
_VERSION_SOURCE = Path("xff/cli/main.cc")


class Finding:
    """One (location, what, version) triple for reporting."""

    def __init__(self, location: str, what: str, version: str):
        self.location = location
        self.what = what
        self.version = version


def _repo_root() -> Path:
    # tools/check_module_versions.py -> repo root is the parent of tools/.
    return Path(__file__).resolve().parent.parent


def _iter_module_files(root: Path):
    for path in sorted(root.rglob("MODULE.bazel")):
        parts = path.relative_to(root).parts
        if any(p == "external" or p.startswith("bazel-") for p in parts):
            continue
        yield path


def _attr(call_body: str, attr: re.Pattern) -> str | None:
    match = attr.search(call_body)
    return match.group(1) if match else None


def collect(root: Path) -> tuple[list[Finding], list[str]]:
    """Return (findings, errors). Errors are structural problems, not disagreements."""
    findings: list[Finding] = []
    errors: list[str] = []

    our_modules: set[str] = set()
    # (rel_path, call_body) for every bazel_dep, revisited once module names are known.
    dep_calls: list[tuple[str, str]] = []

    for path in _iter_module_files(root):
        rel = str(path.relative_to(root))
        text = path.read_text(encoding="utf-8")

        module_match = _MODULE_CALL.search(text)
        if module_match is None:
            errors.append(f"{rel}: no module(...) call found")
            continue
        body = module_match.group(1)
        name = _attr(body, _NAME_ATTR)
        version = _attr(body, _VERSION_ATTR)
        if name is None:
            errors.append(f"{rel}: module(...) has no name")
            continue
        if version is None:
            errors.append(f"{rel}: module(name = {name!r}) has no version")
            continue
        our_modules.add(name)
        findings.append(Finding(rel, f"module {name}", version))

        for dep_match in _BAZEL_DEP.finditer(text):
            dep_calls.append((rel, dep_match.group(1)))

    # Second pass: only intra-repo bazel_deps (a dep on one of our own modules).
    for rel, body in dep_calls:
        name = _attr(body, _NAME_ATTR)
        version = _attr(body, _VERSION_ATTR)
        if name in our_modules and version is not None:
            findings.append(Finding(rel, f"bazel_dep {name}", version))

    version_source = root / _VERSION_SOURCE
    if version_source.is_file():
        literal = _VERSION_LITERAL.search(version_source.read_text(encoding="utf-8"))
        if literal is not None:
            findings.append(Finding(str(_VERSION_SOURCE), "xff --version", literal.group(1)))
        else:
            errors.append(f"{_VERSION_SOURCE}: no `xff <version>` --version literal found")
    else:
        errors.append(f"{_VERSION_SOURCE}: not found (expected the --version literal here)")

    return findings, errors


def _report(findings: list[Finding]) -> None:
    width = max((len(f.location) for f in findings), default=0)
    for f in findings:
        print(f"  {f.location:<{width}}  {f.version:<12}  {f.what}", file=sys.stderr)


def decide(findings: list[Finding], errors: list[str], expected: str | None) -> int:
    """Turn collected findings into an exit code, reporting any disagreement."""
    if errors:
        print("check_module_versions: could not read every version location:", file=sys.stderr)
        for e in errors:
            print(f"  {e}", file=sys.stderr)
        return 1

    if not findings:
        print("check_module_versions: found no version locations to check", file=sys.stderr)
        return 1

    versions = {f.version for f in findings}
    if len(versions) != 1:
        print(
            "check_module_versions: in-repo versions disagree "
            f"({', '.join(sorted(versions))}); all modules and the --version literal "
            "must share one version:",
            file=sys.stderr,
        )
        _report(findings)
        return 1

    (found,) = tuple(versions)
    if expected is not None and found != expected:
        print(
            f"check_module_versions: expected version {expected!r} but every location is "
            f"{found!r}:",
            file=sys.stderr,
        )
        _report(findings)
        return 1

    return 0


def main(argv: list[str]) -> int:
    expected = argv[1] if len(argv) > 1 else None
    findings, errors = collect(_repo_root())
    return decide(findings, errors, expected)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
