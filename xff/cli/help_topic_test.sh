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
# Binary-level test of `--help=TOPIC` topic help (the flag-only mechanism) and the
# guiding error when a user reaches for a `help` subcommand out of habit. Anchors on
# stable substrings, not exact wording.

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

# "yes" if $1 (output) contains the regex $2, else "no" -- for expect_eq. Uses
# `<<<` (not `printf | grep`): a pipe lets grep -q's early exit SIGPIPE a
# still-writing printf, which fails under `set -o pipefail` and misreports "no".
_has() { grep -qE -- "$2" <<<"$1" && echo yes || echo no; }

test::help_topic_flag_prints_entry() {
  local out rc
  out="$("$(_xff_bin)" --help=-regex 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_eq "yes" "$(_has "${out}" '\-regex')"
  expect_eq "yes" "$(_has "${out}" 'regular expression')" # the summary
  expect_eq "yes" "$(_has "${out}" 'test')"               # kind tag
}

test::help_topic_flag_resolves_without_dash() {
  expect_eq "yes" "$(_has "$("$(_xff_bin)" --help=regex 2>&1)" '\-regex')"
}

test::help_list_shows_grouped_index() {
  local out
  out="$("$(_xff_bin)" --help=list 2>&1)"
  expect_eq "yes" "$(_has "${out}" 'Tests:')"
  expect_eq "yes" "$(_has "${out}" 'Actions:')"
  expect_eq "yes" "$(_has "${out}" 'Operators:')"
}

test::help_expressions_lists_the_annotated_vocabulary() {
  # `--help=expressions` is the grouped Tests/Actions/Operators list with summaries,
  # the full list the usage overview points at.
  local out
  out="$("$(_xff_bin)" --help=expressions 2>&1)"
  expect_eq "yes" "$(_has "${out}" 'Tests:')"
  expect_eq "yes" "$(_has "${out}" 'Actions:')"
  expect_eq "yes" "$(_has "${out}" '\-content')" # an expression primary is listed
}

test::help_unknown_topic_exits_two() {
  local out rc
  out="$("$(_xff_bin)" --help=-nonesuch 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_eq "yes" "$(_has "${out}" 'no help topic')"
}

test::bare_help_operand_is_guided_not_a_subcommand() {
  # A user typing `xff help` out of git habit gets a guiding error (not a silent
  # attempt to search a path named "help").
  local out rc
  out="$("$(_xff_bin)" help 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_eq "yes" "$(_has "${out}" 'not a subcommand')"
  expect_eq "yes" "$(_has "${out}" '\-\-help')"
}

test::bare_help_operand_passes_through_in_find_mode() {
  # Invoked as `find`, `help` must stay a path operand (find compatibility), so the
  # xff guiding error must NOT fire.
  local tmp out
  tmp="$(mktemp -d)"
  cp "$(_xff_bin)" "${tmp}/find"
  out="$("${tmp}/find" help 2>&1)" || true
  rm -rf "${tmp}"
  expect_eq "no" "$(_has "${out}" 'not a subcommand')"
}

test_runner
