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
# Binary-level test of --color=auto|always|never. Drives the real binary because
# the color decision reads isatty(stdout): the test captures stdout into a
# variable (a pipe, not a tty), so auto stays plain and only --color=always forces
# the ANSI escapes. Anchors on the presence/absence of the directory color escape.

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

# The ANSI CSI escape for a directory (bold blue) and the SGR reset, as literal
# bytes so grep sees the real ESC (\033). bash 3.2 ANSI-C quoting ($'...').
DIR_COLOR=$'\033\\[1;34m'
RESET=$'\033\\[0m'

# A fresh tree per test with one subdirectory (reliably colorable as a directory).
_make_tree() {
  local root
  root="$(mktemp -d)"
  mkdir -p "${root}/sub"
  printf 'x\n' >"${root}/a.txt"
  echo "${root}"
}

test::color_always_wraps_directories_in_ansi() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --color=always "${root}" -type d 2>&1)"
  rm -rf "${root}"
  expect_matches "${DIR_COLOR}" "${out}"
  expect_matches "${RESET}" "${out}"
}

test::color_never_emits_no_escapes() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --color=never "${root}" -type d 2>&1)"
  rm -rf "${root}"
  expect_not_matches "${DIR_COLOR}" "${out}"
}

test::color_auto_is_plain_when_stdout_is_not_a_tty() {
  # Default (auto): captured stdout is a pipe, so no color even for a directory.
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -type d 2>&1)"
  rm -rf "${root}"
  expect_not_matches "${DIR_COLOR}" "${out}"
}

test::color_always_leaves_plain_files_uncolored() {
  # A non-executable regular file gets no color escape even under --color=always.
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --color=always "${root}" -name a.txt 2>&1)"
  rm -rf "${root}"
  expect_not_matches $'\033\\[' "${out}"
}

test::help_documents_color() {
  # Self-documentation: the --help usage page lists --color in the Output group.
  expect_output_contains "--color" "$("$(_xff_bin)" --help 2>&1)"
}

test_runner
