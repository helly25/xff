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

test::help_topic_flag_prints_entry() {
  local out rc
  out="$("$(_xff_bin)" --help=-regex 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_matches '\-regex' "${out}"
  expect_output_contains 'regular expression' "${out}" # the summary
  expect_output_contains 'test' "${out}"               # kind tag
}

test::help_topic_flag_resolves_without_dash() {
  expect_matches '\-regex' "$("$(_xff_bin)" --help=regex 2>&1)"
}

test::help_list_shows_grouped_index() {
  local out
  out="$("$(_xff_bin)" --help=list 2>&1)"
  expect_output_contains 'Tests:' "${out}"
  expect_output_contains 'Actions:' "${out}"
  expect_output_contains 'Operators:' "${out}"
}

test::help_expressions_lists_the_annotated_vocabulary() {
  # `--help=expressions` is the grouped Tests/Actions/Operators list with summaries,
  # the full list the usage overview points at.
  local out
  out="$("$(_xff_bin)" --help=expressions 2>&1)"
  expect_output_contains 'Tests:' "${out}"
  expect_output_contains 'Actions:' "${out}"
  expect_matches '\-content' "${out}" # an expression primary is listed
}

test::help_unknown_topic_exits_two() {
  local out rc
  out="$("$(_xff_bin)" --help=-nonesuch 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains 'no help topic' "${out}"
}

test::bare_help_operand_is_guided_not_a_subcommand() {
  # A user typing `xff help` out of git habit gets a guiding error (not a silent
  # attempt to search a path named "help").
  local out rc
  out="$("$(_xff_bin)" help 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains 'not a subcommand' "${out}"
  expect_matches '\-\-help' "${out}"
}

test::bare_help_operand_passes_through_in_find_mode() {
  # Invoked as `find`, `help` must stay a path operand (find compatibility), so the
  # xff guiding error must NOT fire.
  local tmp out
  tmp="$(mktemp -d)"
  cp "$(_xff_bin)" "${tmp}/find"
  out="$("${tmp}/find" help 2>&1)" || true
  rm -rf "${tmp}"
  expect_output_not_contains 'not a subcommand' "${out}"
}

test_runner
