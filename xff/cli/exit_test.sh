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
# Binary-level test of the exit-code model (design.md review #9): the default is
# find semantics (0 ran / 2 error; match status never affects exit), while --quiet
# (suppress output, exit by match) and --exit-match (keep output, exit by match)
# make "1 = no match" reachable. An error always outranks match status (exit 2).
# Exit codes are inherently a binary-level concern, so this drives the real binary.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${helly25_bashtest}"

# Path to the built xff binary in the test runfiles.
_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# A directory holding a single file, isolated from any ambient config.
_tree() {
  local dir="${TEST_TMPDIR}/$1"
  mkdir -p "${dir}"
  : >"${dir}/a.txt"
  echo "${dir}"
}

test::default_no_match_still_exits_zero() {
  local dir
  dir="$(_tree default)"
  # No --quiet/--exit-match: match status must not affect the exit (find semantics).
  # Nothing matches "nope.zzz", yet the run exits 0 because it ran without error.
  local rc
  XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name 'nope.zzz' >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "0" "${rc}"
}

test::quiet_match_exits_zero_with_no_output() {
  local dir out rc
  dir="$(_tree qmatch)"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --quiet "${dir}" -name a.txt 2>/dev/null)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_eq "" "${out}" # --quiet suppresses output
}

test::quiet_no_match_exits_one() {
  local dir out rc
  dir="$(_tree qnomatch)"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --quiet "${dir}" -name 'nope.zzz' 2>/dev/null)" && rc=0 || rc=$?
  expect_eq "1" "${rc}"
  expect_eq "" "${out}"
}

test::exit_match_keeps_output_on_match() {
  local dir out rc
  dir="$(_tree emmatch)"
  # --exit-match exits by match but, unlike --quiet, keeps the normal output.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --exit-match "${dir}" -name a.txt 2>/dev/null)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  local lines=()
  local line
  while IFS= read -r line; do lines+=("${line}"); done <<<"${out}"
  expect_contains "${dir}/a.txt" "${lines[@]}"
}

test::exit_match_no_match_exits_one() {
  local dir rc
  dir="$(_tree emnomatch)"
  XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --exit-match "${dir}" -name 'nope.zzz' >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "1" "${rc}"
}

test::error_outranks_match_status() {
  local rc
  # A missing root is a per-path error; even under --quiet (where no match would be
  # exit 1) the error wins and the exit is 2.
  XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --quiet "${TEST_TMPDIR}/does-not-exist" >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "2" "${rc}"
}

test::dash_q_is_a_grep_alias_of_quiet() {
  local dir out rc
  dir="$(_tree qalias)"
  # -q behaves exactly like --quiet: suppress output, exit by match.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" -q "${dir}" -name a.txt 2>/dev/null)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_eq "" "${out}"
  XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" -q "${dir}" -name 'nope.zzz' >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "1" "${rc}"
}

test::unknown_global_flag_is_a_usage_error() {
  local dir out rc
  dir="$(_tree unknownflag)"
  # An unrecognized leading option is a usage error (exit 2), not silently ignored.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --bogus-flag "${dir}" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "unknown option" "${out}" # prominent, actionable message
  expect_output_contains "--bogus-flag" "${out}"   # names the offending flag
}

test::known_global_flag_is_accepted() {
  local dir rc
  dir="$(_tree knownflag)"
  # A valid leading global (--sort) is accepted; the run exits 0.
  XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --sort "${dir}" -name a.txt >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "0" "${rc}"
}

test_runner
