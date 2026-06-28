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
# Binary-level test that `--help` / `-h` print a real usage page (not a stub) and
# exit 0, and that `--version` prints a version and exits 0. Anchors on a few
# stable substrings rather than exact wording, so prose edits do not break it.

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

# "yes" if $1 (output) contains the regex $2, else "no" -- for expect_eq.
_has() { printf '%s\n' "$1" | grep -qE -- "$2" && echo yes || echo no; }

test::help_prints_usage_and_options() {
  local out rc
  out="$("$(_xff_bin)" --help 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_eq "yes" "$(_has "${out}" 'Usage:')"
  expect_eq "yes" "$(_has "${out}" '\-\-config')" # a shipped global, not the old stub
  expect_eq "yes" "$(_has "${out}" '\-\-quiet')"
  expect_eq "yes" "$(_has "${out}" 'Expression:')"
  local n
  n="$(printf '%s\n' "${out}" | wc -l)"
  # The old stub was two lines; a real page is many more.
  expect_eq "yes" "$([[ ${n} -ge 10 ]] && echo yes || echo no)"
}

test::dash_h_matches_long_help() {
  local short long
  short="$("$(_xff_bin)" -h 2>&1)"
  long="$("$(_xff_bin)" --help 2>&1)"
  expect_eq "${long}" "${short}"
}

test::version_prints_and_exits_zero() {
  local out rc
  out="$("$(_xff_bin)" --version 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_eq "yes" "$(_has "${out}" 'xff')"
}

test::gnu_single_dash_help_matches_long_help() {
  # GNU find compatibility: -help is the single-dash long option for --help.
  local single long
  single="$("$(_xff_bin)" -help 2>&1)"
  long="$("$(_xff_bin)" --help 2>&1)"
  expect_eq "${long}" "${single}"
}

test::gnu_single_dash_version_prints_and_exits_zero() {
  # GNU find compatibility: -version is the single-dash long option for --version.
  local out rc
  out="$("$(_xff_bin)" -version 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_eq "yes" "$(_has "${out}" 'xff')"
}

test::man_prints_roff_and_exits_zero() {
  local out rc
  out="$("$(_xff_bin)" --man 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_eq "yes" "$(_has "${out}" '^\.TH xff 1')" # a roff man page header
  expect_eq "yes" "$(_has "${out}" '^\.SH OPTIONS')"
}

test_runner
