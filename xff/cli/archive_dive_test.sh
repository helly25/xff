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
# End-to-end test of --archive DIVING, the other side of archive_test.sh: that one pins what a
# build WITHOUT the extra does, this one runs the binary that has it (the target is gated on
# --//xff:xff_archive) and asserts the three modes actually differ on a real archive.
#
# The fixtures are built here with `tar` rather than committed: the committed ones live in the
# archive extra's own module, which the core cannot depend on, and a two-file tar written at test
# time is both cheaper and self-describing.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${helly25_bashtest}"

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/xff_full"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff_full -path '*xff/cli/xff_full' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# A directory holding `a.tar` (members `one.txt` and `dir/two.txt`) plus a plain `b.txt`, so one
# tree exercises "the container", "inside the container" and "an ordinary file" at once.
_tree() {
  local root stage
  root="$(mktemp -d)"
  stage="${root}/stage"
  mkdir -p "${stage}/dir"
  echo "needle" >"${stage}/one.txt"
  echo "two" >"${stage}/dir/two.txt"
  # COPYFILE_DISABLE keeps macOS bsdtar from adding an AppleDouble `._name` member beside every
  # real one, which would make the member list platform-dependent.
  COPYFILE_DISABLE=1 tar -c -f "${root}/a.tar" -C "${stage}" one.txt dir
  rm -rf "${stage}"
  : >"${root}/b.txt"
  echo "${root}"
}

test::a_container_named_as_a_root_lists_its_members() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar")"
  # Dual identity: the tar is still the file it is, and its members follow it.
  expect_output_contains "${root}/a.tar" "${out}"
  expect_output_contains "a.tar!one.txt" "${out}"
  expect_output_contains "a.tar!dir/two.txt" "${out}"
  rm -rf "${root}"
}

test::none_keeps_an_archive_a_plain_file() {
  local root out
  root="$(_tree)"
  for spelling in "--archive=none" "-z-"; do
    out="$("$(_xff_bin)" "${spelling}" "${root}/a.tar")"
    expect_output_contains "${root}/a.tar" "${out}"
    expect_output_not_contains "a.tar!" "${out}"
  done
  rm -rf "${root}"
}

test::roots_does_not_dive_into_an_archive_found_mid_walk() {
  local root out
  root="$(_tree)"
  # `roots` is "the archive I pointed you AT". Walking the DIRECTORY finds the same tar, and must
  # leave it closed - that difference is the whole point of having two modes.
  out="$("$(_xff_bin)" --archive=roots "${root}")"
  expect_output_contains "${root}/a.tar" "${out}"
  expect_output_not_contains "a.tar!" "${out}"
  rm -rf "${root}"
}

test::all_dives_into_an_archive_found_mid_walk() {
  local root out
  root="$(_tree)"
  for spelling in "--archive=all" "-z+" "--archive"; do
    out="$("$(_xff_bin)" "${spelling}" "${root}")"
    expect_output_contains "a.tar!one.txt" "${out}"
    expect_output_contains "a.tar!dir/two.txt" "${out}"
    expect_output_contains "${root}/b.txt" "${out}"  # an ordinary file is untouched by diving
  done
  rm -rf "${root}"
}

test::members_are_ordinary_entries_for_the_expression() {
  local root out
  root="$(_tree)"
  # Nothing in the vocabulary knows about archives: a member is matched by the same -name / -type
  # every other entry is, which is what makes diving worth having.
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar" -type f -name '*.txt')"
  expect_output_contains "a.tar!one.txt" "${out}"
  expect_output_contains "a.tar!dir/two.txt" "${out}"
  # Exactly those two: the container is not a `.txt`, and `dir` is not a file.
  expect_eq "2" "$(grep -c . <<<"${out}")"
  rm -rf "${root}"
}

test::pruning_the_container_keeps_the_file_and_skips_the_members() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar" -name 'a.tar' -prune -print)"
  expect_output_contains "${root}/a.tar" "${out}"
  expect_output_not_contains "a.tar!" "${out}"
  rm -rf "${root}"
}

test::the_member_path_separator_and_prefix_are_the_users_choice() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" --archive=roots --archive-separator='#' "${root}/a.tar")"
  expect_output_contains "a.tar#one.txt" "${out}"
  out="$("$(_xff_bin)" --archive=roots --archive-prefix=URI "${root}/a.tar")"
  expect_output_contains "archive://${root}/a.tar!one.txt" "${out}"
  rm -rf "${root}"
}

test::an_ordinary_file_that_is_not_an_archive_is_no_error() {
  local root out rc
  root="$(_tree)"
  # Every plain file the walk meets under `all` is offered to the reader and declined; that must be
  # silent, or `xff -z+ .` would report an error per ordinary file.
  out="$("$(_xff_bin)" --archive=all "${root}" 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_not_contains "xff:" "${out}"
  rm -rf "${root}"
}

test_runner
