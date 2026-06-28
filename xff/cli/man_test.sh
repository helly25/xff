#!/usr/bin/env bash
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
#
# End-to-end test of `xff --man`: it emits roff source, and that source renders
# through a real roff formatter (mandoc / groff / nroff) into a formatted man page
# with the expected sections and no leftover roff control lines. The formatter is a
# required dependency (mandoc on macOS, installed in CI on Linux); a missing one is a
# hard failure, not a skip.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${helly25_bashtest}"

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# "yes" if $1 (text) matches the regex $2, else "no" -- for expect_eq.
_has() { printf '%s\n' "$1" | grep -qE -- "$2" && echo yes || echo no; }

test::man_renders_through_a_roff_formatter() {
  local roff tmp rendered renderer=""
  roff="$("$(_xff_bin)" --man)"
  tmp="$(mktemp)"
  printf '%s\n' "${roff}" >"${tmp}"

  # Use whatever formatter the host has; col -b flattens bold/underline overstriking
  # so the assertions match plain text.
  if command -v mandoc >/dev/null 2>&1; then
    renderer="mandoc"
    rendered="$(mandoc -Tascii "${tmp}" 2>/dev/null | col -b)"
  elif command -v groff >/dev/null 2>&1; then
    renderer="groff"
    rendered="$(groff -man -Tascii "${tmp}" 2>/dev/null | col -b)"
  elif command -v nroff >/dev/null 2>&1; then
    renderer="nroff"
    rendered="$(nroff -man "${tmp}" 2>/dev/null | col -b)"
  fi
  rm -f "${tmp}"

  # A roff formatter is a required test dependency: mandoc ships on macOS and is
  # installed in CI on Linux. Its absence is a hard failure, not a skip.
  if [[ -z "${renderer}" ]]; then
    echo "ERROR: no roff formatter found; install mandoc (or groff/nroff)"
    expect_eq "a roff formatter (mandoc/groff/nroff) on PATH" "none found"
    return 0
  fi

  echo "--- rendered with ${renderer} (first 20 lines) ---"
  printf '%s\n' "${rendered}" | head -20

  # The formatter produced a real man page: title (mandoc keeps `xff(1)`, groff/man-db
  # upper-case to `XFF(1)`, so match either) + section headings and a documented option.
  expect_eq "yes" "$(_has "${rendered}" '[Xx][Ff][Ff]\(1\)')"
  expect_eq "yes" "$(_has "${rendered}" 'NAME')"
  expect_eq "yes" "$(_has "${rendered}" 'OPTIONS')"
  expect_eq "yes" "$(_has "${rendered}" '\-\-sort')"
  # All roff was consumed: no control request survives into the output, neither at
  # line start nor mid-line (a `.B`/`.SH`/... left mid-sentence is printed literally).
  expect_eq "no" "$(_has "${rendered}" '^\.[A-Za-z]')"
  expect_eq "no" "$(_has "${rendered}" '\.(B|BR|SH|SS|TP|PP|TH) ')"
}

test_runner
