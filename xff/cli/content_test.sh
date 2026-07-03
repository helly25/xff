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
# Binary-level test of the xff content-search predicates: -content / -icontent
# (literal substring) and -rxc / -irxc (regex), the binary-file skip, and the
# find-style gating (these are xff extensions). Drives the real binary because the
# predicates read files on disk; anchors on the matched paths, not exact wording.

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

# A fresh tree per test: text files with known content plus one binary file whose
# NUL byte marks it binary (so content search skips it even though it has a match).
_make_tree() {
  local root
  root="$(mktemp -d)"
  printf 'alpha BETA gamma\n' >"${root}/a.txt"
  printf 'nothing to see here\n' >"${root}/b.txt"
  printf 'id=42 token=abc\n' >"${root}/c.log"
  {
    printf 'ELF'
    printf '\000'
    printf 'needle inside\n'
  } >"${root}/bin.dat"
  echo "${root}"
}

test::content_matches_literal_substring() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -content 'BETA' 2>&1)"
  rm -rf "${root}"
  expect_matches 'a\.txt' "${out}"
  expect_not_matches 'b\.txt' "${out}"
}

test::content_treats_the_pattern_as_literal_not_regex() {
  # '.' is a literal here; "alph." matches "alpha" as a regex but not as a substring.
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -content 'alph.' 2>&1)"
  rm -rf "${root}"
  expect_not_matches 'a\.txt' "${out}"
}

test::content_is_case_sensitive_icontent_folds() {
  local root out_cs out_ci
  root="$(_make_tree)"
  out_cs="$("$(_xff_bin)" "${root}" -content 'beta' 2>&1)"  # lower-case: no match
  out_ci="$("$(_xff_bin)" "${root}" -icontent 'beta' 2>&1)" # folds case: matches
  rm -rf "${root}"
  expect_not_matches 'a\.txt' "${out_cs}"
  expect_matches 'a\.txt' "${out_ci}"
}

test::rxc_matches_a_regular_expression() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -rxc 'id=[0-9]+' 2>&1)"
  rm -rf "${root}"
  expect_matches 'c\.log' "${out}"
  expect_not_matches 'a\.txt' "${out}"
}

test::binary_files_are_skipped() {
  # 'needle' is present in bin.dat, but its NUL byte marks it binary -> not matched.
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -content 'needle' 2>&1)"
  rm -rf "${root}"
  expect_not_matches 'bin\.dat' "${out}"
}

test::content_is_rejected_in_strict_find_style() {
  # -content is an xff extension; --config=find rejects it as a usage error (exit 2).
  local root out rc
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --config=find "${root}" -content 'BETA' 2>&1)" && rc=0 || rc=$?
  rm -rf "${root}"
  expect_eq "2" "${rc}"
}

test::help_topic_documents_content() {
  # Self-documentation flows from the registry: --help=-content renders its summary.
  expect_output_contains "content" "$("$(_xff_bin)" --help=-content 2>&1)"
}

test_runner
