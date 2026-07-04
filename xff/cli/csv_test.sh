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
# End-to-end test of the tabular --format family: csv / tsv stream a header row by default
# (suppressed by --no-header) with RFC-4180 quoting; aligned / markdown buffer the whole
# table and render it padded (a dashed rule under the header) after the walk. --columns and
# its usage errors are shared by all four. Drives the real binary (the header / table is
# emitted by the walk driver, so shell level is the right fit).

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

test::csv_emits_a_header_then_quotes_a_comma_field() {
  local dir out
  dir="${TEST_TMPDIR}/csv"
  mkdir -p "${dir}"
  : >"${dir}/plain.txt"
  : >"${dir}/with,comma.txt" # a comma in the name must be RFC-4180 quoted
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=csv "${dir}" -type f 2>&1)"
  # The header row is emitted first (before any record).
  expect_eq "path" "$(head -1 <<<"${out}")"
  expect_output_contains "plain.txt" "${out}"
  expect_matches '"[^"]*with,comma[^"]*"' "${out}" # the comma field is wrapped in quotes
}

test::no_header_suppresses_the_header_row() {
  local dir out
  dir="${TEST_TMPDIR}/nohdr"
  mkdir -p "${dir}"
  : >"${dir}/a.txt"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=csv --no-header "${dir}" -type f 2>&1)"
  expect_ne "path" "$(head -1 <<<"${out}")" # first line is a record, not the header
  expect_output_contains "a.txt" "${out}"
}

test::tsv_has_a_header_and_tab_separated_records() {
  local dir out
  dir="${TEST_TMPDIR}/tsv"
  mkdir -p "${dir}"
  : >"${dir}/a.txt"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=tsv "${dir}" -type f 2>&1)"
  expect_eq "path" "$(head -1 <<<"${out}")"
  expect_output_contains "a.txt" "${out}"
}

test::columns_produce_a_multi_column_table_with_a_header() {
  local dir out
  dir="${TEST_TMPDIR}/cols"
  mkdir -p "${dir}"
  echo hi >"${dir}/a.txt" # 3 bytes (hi + newline)
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=csv --columns=name,size,type "${dir}" -type f 2>&1)"
  expect_eq "name,size,type" "$(head -1 <<<"${out}")" # the header is the column names
  expect_output_contains "a.txt,3,f" "${out}"
}

test::columns_validation_is_a_usage_error() {
  local dir out rc
  dir="${TEST_TMPDIR}/colerr"
  mkdir -p "${dir}"
  : >"${dir}/a.txt"
  # An unknown column name.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=csv --columns=name,bogus "${dir}" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "unknown column 'bogus'" "${out}"
  # --columns without a tabular format.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --columns=name "${dir}" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "needs a tabular --format" "${out}"
  # --format=csv with an output action (-ls) that suppresses the default listing.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=csv "${dir}" -ls 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "format the default listing" "${out}"
}

test::aligned_renders_a_padded_table_under_a_dashed_rule() {
  local dir out
  dir="${TEST_TMPDIR}/aligned"
  mkdir -p "${dir}"
  echo hi >"${dir}/a.txt"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=aligned --columns=name,size "${dir}" -type f 2>&1)"
  expect_output_contains "name" "${out}" # the header column names
  expect_output_contains "size" "${out}"
  expect_matches "----" "${out}" # a dashed underline separates the header from the rows
  expect_output_contains "a.txt" "${out}"
}

test::markdown_renders_a_github_table_and_md_is_its_alias() {
  local dir out
  dir="${TEST_TMPDIR}/md"
  mkdir -p "${dir}"
  echo hi >"${dir}/a.txt"
  # --format=md is the short alias of --format=markdown.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=md --columns=name,size "${dir}" -type f 2>&1)"
  expect_matches "[|] -+ [|] -+ [|]" "${out}" # the padded | --- | --- | rule
  expect_output_contains "| a.txt" "${out}"
}

test::no_header_drops_the_buffered_table_header() {
  local dir out
  dir="${TEST_TMPDIR}/aligned_nohdr"
  mkdir -p "${dir}"
  echo hi >"${dir}/a.txt"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=aligned --no-header --columns=name,size "${dir}" -type f 2>&1)"
  expect_output_not_contains "name" "${out}" # the header row (and its rule) is gone
  expect_output_contains "a.txt" "${out}"
}

test_runner
