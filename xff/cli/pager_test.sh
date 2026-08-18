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
# Binary-level test of --pager. Drives the real binary because paging reads
# isatty(stdout): the test captures stdout into a variable (a pipe, not a tty),
# so auto never pages. A pass-through pager (XFF_PAGER=cat) proves the piped text
# is the same bytes as the unpaged output, and an empty pager env disables paging.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${helly25_bashtest}"

# Scratch trees live under bashtest's own ${BASHTEST_TMPDIR}, which its exit trap removes; the name
# is a per-call counter, so a case that builds two trees needs nothing special and no test name
# leaks into a printed path (where it could satisfy an expect_output_not_contains).

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

test::pager_never_prints_help_to_stdout() {
  expect_output_contains "eXtended File Find" "$("$(_xff_bin)" --pager=never --help 2>&1)"
}

test::pager_always_pipes_through_the_pager_verbatim() {
  # A cat pager is a pass-through: --pager=always feeds the help through it, so the
  # bytes are identical to the unpaged --pager=never output.
  local bin paged plain
  bin="$(_xff_bin)"
  paged="$(XFF_PAGER='cat' "${bin}" --pager=always --help 2>&1)"
  plain="$("${bin}" --pager=never --help 2>&1)"
  expect_eq "${plain}" "${paged}"
}

test::pager_auto_does_not_page_when_stdout_is_not_a_tty() {
  # Default (auto): captured stdout is a pipe, so a would-hang pager is never run and
  # the output equals --pager=never. A blocking pager (sleep) would deadlock the test
  # if auto paged here.
  local bin auto plain
  bin="$(_xff_bin)"
  auto="$(XFF_PAGER='sleep 30' "${bin}" --help 2>&1)"
  plain="$("${bin}" --pager=never --help 2>&1)"
  expect_eq "${plain}" "${auto}"
}

test::empty_pager_env_disables_paging() {
  # An explicitly-empty $XFF_PAGER means "no pager", so even --pager=always falls back
  # to stdout (and does not hang on a would-be pager).
  expect_output_contains "eXtended File Find" "$(XFF_PAGER='' "$(_xff_bin)" --pager=always --help 2>&1)"
}

test::no_pager_alias_is_accepted_on_a_real_search() {
  # --no-pager is a recognized global (alias for --pager=never), so it does not trip the
  # unknown-flag error on an ordinary search.
  local root out
  _xff_tree_seq=$((${_xff_tree_seq:-0} + 1))
  root="${BASHTEST_TMPDIR}/tree${_xff_tree_seq}"
  mkdir -p "${root}"
  printf 'x\n' >"${root}/a.txt"
  out="$("$(_xff_bin)" "${root}" -type f --no-pager 2>&1)"
  expect_output_contains "a.txt" "${out}"
}

test::man_pager_never_emits_raw_roff() {
  # --pager=never keeps --man as raw roff source (for `mandoc` / redirect / install),
  # so the roff title macro is present verbatim.
  expect_output_contains ".TH " "$("$(_xff_bin)" --pager=never --man 2>&1)"
}

test::man_pager_always_routes_through_the_man_pager_verbatim() {
  # A cat man-pager is a pass-through, so --pager=always --man feeds the roff through it
  # unchanged - identical to the raw --pager=never roff. Proves the man path is taken and
  # uses $XFF_MANPAGER (not the mandoc default, so the test does not depend on mandoc).
  local bin paged raw
  bin="$(_xff_bin)"
  paged="$(XFF_MANPAGER='cat' "${bin}" --pager=always --man 2>&1)"
  raw="$("${bin}" --pager=never --man 2>&1)"
  expect_eq "${raw}" "${paged}"
}

test::man_pager_auto_stays_raw_off_a_tty() {
  # Default (auto): captured stdout is a pipe, so --man is not paged/formatted and stays
  # raw roff. A blocking man-pager (sleep) would deadlock the test if auto paged here.
  local bin auto raw
  bin="$(_xff_bin)"
  auto="$(XFF_MANPAGER='sleep 30' "${bin}" --man 2>&1)"
  raw="$("${bin}" --pager=never --man 2>&1)"
  expect_eq "${raw}" "${auto}"
}

test::pager_all_leaves_a_piped_listing_alone() {
  # The listing value is terminal-only: captured stdout is a pipe, so --pager=all must not start a
  # pager (a blocking one would deadlock this test) and the listing must arrive verbatim.
  local root bin all plain
  _xff_tree_seq=$((${_xff_tree_seq:-0} + 1))
  root="${BASHTEST_TMPDIR}/tree${_xff_tree_seq}"
  mkdir -p "${root}"
  printf 'x\n' >"${root}/a.txt"
  bin="$(_xff_bin)"
  all="$(XFF_PAGER='sleep 30' "${bin}" --pager=all "${root}" -type f 2>&1)"
  plain="$("${bin}" --pager=never "${root}" -type f 2>&1)"
  expect_eq "${plain}" "${all}"
}

test::help_documents_pager() {
  # Self-documentation: the --help usage page lists --pager in the Output group.
  expect_output_contains "--pager" "$("$(_xff_bin)" --help 2>&1)"
  # And the listing value, whose help has to say what it adds and when it steps aside.
  local out
  out="$("$(_xff_bin)" --help=--pager 2>&1)"
  expect_output_contains "all" "${out}"
  expect_output_contains "-ok" "${out}" # the primaries that suppress it
}

test_runner
