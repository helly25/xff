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
"""Assert that ``--config=xff_docs`` turns on EVERY composable extra.

The committed reference (``XFF.md``) is generated under ``--config=xff_docs`` so it documents the
whole tool. That config is a hand-written list of extras, and the failure mode is SILENT: add a new
extra, forget this line, and the published reference quietly omits the feature - no build breaks, no
test fails, the docs are just incomplete. So the list is checked rather than remembered.

The extras are the ``bool_flag`` declarations in ``xff/BUILD.bazel`` (each one gates an extra via its
``<name>_enabled`` ``config_setting``), minus an explicit allowlist for flags that are NOT extras.
``--config=xff_docs`` may enable a flag directly or inherit it from a config it includes (it builds on
``xff_full``), so the whole ``--config`` chain is followed.

Usage:
    check_docs_extras.py            # checks .bazelrc against xff/BUILD.bazel
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

_BUILD = Path("xff/BUILD.bazel")
_BAZELRC = Path(".bazelrc")
_DOCS_CONFIG = "xff_docs"

# Build flags that are not composable extras, and why. A `bool_flag` here is a build knob, not a
# feature the reference should document as available.
_NOT_EXTRAS: dict[str, str] = {
    "xff_msan": "a sanitizer knob (gates the instrumented libc++ dep), not a user-facing feature",
}

_BOOL_FLAG = re.compile(r'bool_flag\(\s*name\s*=\s*"([^"]+)"', re.DOTALL)


def declared_extras(build_text: str) -> list[str]:
    """Every `bool_flag` in xff/BUILD.bazel that represents a composable extra."""
    return [name for name in _BOOL_FLAG.findall(build_text) if name not in _NOT_EXTRAS]


def enabled_flags(bazelrc_text: str, config: str, _seen: set[str] | None = None) -> set[str]:
    """Flags a `--config=<config>` turns on, following `--config=` inheritance."""
    seen = _seen if _seen is not None else set()
    if config in seen:
        return set()  # a cycle would hang; treat it as contributing nothing
    seen.add(config)
    enabled: set[str] = set()
    for line in bazelrc_text.splitlines():
        stripped = line.strip()
        if not stripped.startswith(f"common:{config} ") and not stripped.startswith(f"build:{config} "):
            continue
        body = stripped.split(None, 1)[1] if len(stripped.split(None, 1)) > 1 else ""
        body = body.split("#", 1)[0].strip()  # drop a trailing comment
        if match := re.match(r"--config=(\S+)", body):
            enabled |= enabled_flags(bazelrc_text, match.group(1), seen)
            continue
        # `--//xff:xff_archive=True` (or `=true`); a bare `--//xff:x` also means on.
        if match := re.match(r"--//xff:(\w+)(?:=(\S+))?$", body):
            value = (match.group(2) or "true").lower()
            if value in ("true", "1", "yes"):
                enabled.add(match.group(1))
    return enabled


def main() -> int:
    if not _BUILD.is_file() or not _BAZELRC.is_file():
        print(f"ERROR: run from the workspace root (need {_BUILD} and {_BAZELRC})", file=sys.stderr)
        return 1
    extras = declared_extras(_BUILD.read_text())
    enabled = enabled_flags(_BAZELRC.read_text(), _DOCS_CONFIG)
    missing = [name for name in extras if name not in enabled]
    if not missing:
        return 0
    print(
        f"--config={_DOCS_CONFIG} must enable EVERY composable extra, so the committed XFF.md "
        f"documents the whole tool. Missing:",
        file=sys.stderr,
    )
    for name in missing:
        print(f"  --//xff:{name}", file=sys.stderr)
    print(
        f"Add `common:{_DOCS_CONFIG} --//xff:<name>=True` to .bazelrc (or list the flag in "
        f"_NOT_EXTRAS in {Path(__file__).name} if it is not an extra, with the reason).",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
