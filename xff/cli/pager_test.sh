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
  root="$(mktemp -d)"
  printf 'x\n' >"${root}/a.txt"
  out="$("$(_xff_bin)" "${root}" -type f --no-pager 2>&1)"
  rm -rf "${root}"
  expect_output_contains "a.txt" "${out}"
}

test::help_documents_pager() {
  # Self-documentation: the --help usage page lists --pager in the Output group.
  expect_output_contains "--pager" "$("$(_xff_bin)" --help 2>&1)"
}

test_runner
