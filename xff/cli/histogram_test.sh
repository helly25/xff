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
# End-to-end test of --histogram: ASCII vs Unicode bars (via --unicode), the count per bucket
# sorted by height, suppression of the per-match listing, combining with --summary, and the
# unknown-bucket usage error. Drives the real binary.

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

# A tree with three .cc and one .h, so the ext histogram is cc=3 (tallest) and h=1.
_make_tree() {
  local dir="$1"
  mkdir -p "${dir}"
  local f
  for f in a.cc b.cc c.cc d.h; do echo x >"${dir}/${f}"; done
}

test::histogram_ascii_bars_scaled_to_tallest() {
  local dir out
  dir="${TEST_TMPDIR}/histascii"
  _make_tree "${dir}"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --histogram=ext --unicode=never "${dir}" -type f 2>&1)"
  # cc (3) and h (1) each get a bar of '#'; cc is the tallest so it is the widest.
  expect_matches "cc[[:space:]]+3[[:space:]]+#+" "${out}"
  expect_matches "h[[:space:]]+1[[:space:]]+#+" "${out}"
  # The per-match listing is suppressed: the file paths never appear.
  expect_output_not_contains "a.cc" "${out}"
}

test::histogram_unicode_bars() {
  local dir out
  dir="${TEST_TMPDIR}/histuni"
  _make_tree "${dir}"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --histogram=ext --unicode=always "${dir}" -type f 2>&1)"
  # Unicode block bars use the full-block character.
  expect_output_contains "█" "${out}"
}

test::histogram_combines_with_summary() {
  local dir out
  dir="${TEST_TMPDIR}/histsum"
  _make_tree "${dir}"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --summary=type --histogram=ext --unicode=never "${dir}" -type f 2>&1)"
  # The --summary table (a total row) and the histogram bars both appear.
  expect_output_contains "total" "${out}"
  expect_matches "cc[[:space:]]+3[[:space:]]+#+" "${out}"
}

test::histogram_unknown_bucket_is_a_usage_error() {
  local dir out rc
  dir="${TEST_TMPDIR}/histerr"
  mkdir -p "${dir}"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --histogram=bogus "${dir}" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "unknown --histogram bucket" "${out}"
}

test_runner
