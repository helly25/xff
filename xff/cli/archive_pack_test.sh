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
# End-to-end test of --pack: the walk BUILDS an archive instead of listing. The writer has its own
# unit tests in the extra; what only this level can prove is the wiring - that the member names come
# out relative to the search root, that the sink really replaces the listing, that the pre-walk
# checks fire before anything is written, and that the archive the walk meets is not packed into
# itself. Read back with the system `tar`, not with xff, so a bug shared by both readers cannot hide.

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

# A small tree: `src/a.cc`, `src/sub/b.cc` and `src/c.txt`, so an expression can select a subset and
# the nested file proves the relative naming.
_tree() {
  local root
  root="$(mktemp -d)"
  mkdir -p "${root}/src/sub"
  echo "first" >"${root}/src/a.cc"
  echo "second" >"${root}/src/sub/b.cc"
  echo "other" >"${root}/src/c.txt"
  echo "${root}"
}

test::packed_members_are_named_relative_to_their_search_root() {
  local root out members
  root="$(_tree)"
  out="$("$(_xff_bin)" "${root}/src" -name '*.cc' --pack="${root}/out.tar" 2>&1)"
  expect_eq "" "${out}" # a sink: the listing is replaced, and a plain pack says nothing
  members="$(tar -tf "${root}/out.tar")"
  # `a.cc`, not `${root}/src/a.cc`: an absolute path baked into an archive unpacks somewhere nobody
  # asked for, so the root the entry was found under is stripped.
  expect_output_contains "a.cc" "${members}"
  expect_output_contains "sub/b.cc" "${members}"
  expect_output_not_contains "c.txt" "${members}"
  expect_output_not_contains "${root}" "${members}"
}

test::an_explicit_print_still_lists_what_goes_in() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" "${root}/src" -name '*.cc' -print --pack="${root}/out.tar")"
  # The sink suppresses the IMPLICIT print only; an action the user wrote still runs.
  expect_output_contains "${root}/src/a.cc" "${out}"
  expect_output_contains "${root}/src/sub/b.cc" "${out}"
}

test::the_member_content_survives_the_round_trip() {
  local root
  root="$(_tree)"
  "$(_xff_bin)" "${root}/src" -name 'a.cc' --pack="${root}/out.tar.gz"
  expect_eq "first" "$(tar -xOzf "${root}/out.tar.gz" a.cc)"
}

test::an_output_name_with_no_writable_format_fails_before_the_walk() {
  local root status out
  root="$(_tree)"
  status=0
  out="$("$(_xff_bin)" "${root}/src" --pack="${root}/out.rar" 2>&1)" || status=$?
  expect_eq 2 "${status}"
  expect_output_contains "cannot tell the archive format" "${out}"
  expect_output_contains "tar.gz" "${out}" # the accepted set is named, not left to be guessed
  expect_eq "0" "$(find "${root}" -maxdepth 1 -name 'out.*' | wc -l | tr -d ' ')"
}

test::a_compression_level_the_format_rejects_is_a_usage_error() {
  local root status out
  root="$(_tree)"
  status=0
  out="$("$(_xff_bin)" "${root}/src" --pack="${root}/out.tar.gz" --pack-level=99 2>&1)" || status=$?
  expect_eq 2 "${status}"
  expect_output_contains "accepts 0-9" "${out}"
  # And a plain tar has nothing to set a level on, which is an error rather than a silent no-op.
  status=0
  out="$("$(_xff_bin)" "${root}/src" --pack="${root}/plain.tar" --pack-level=9 2>&1)" || status=$?
  expect_eq 2 "${status}"
  expect_output_contains "not compressed" "${out}"
}

test::a_level_that_works_produces_a_smaller_archive() {
  local root fast small
  root="$(_tree)"
  # Compressible input, so the two levels cannot come out the same size by accident.
  head -c 200000 /dev/zero | tr '\0' 'a' >"${root}/src/big.txt"
  "$(_xff_bin)" "${root}/src" -name 'big.txt' --pack="${root}/fast.tar.gz" --pack-level=1
  "$(_xff_bin)" "${root}/src" -name 'big.txt' --pack="${root}/small.tar.gz" --pack-level=9
  fast="$(wc -c <"${root}/fast.tar.gz")"
  small="$(wc -c <"${root}/small.tar.gz")"
  # bashtest has no ordering matcher, so the comparison is the value under test.
  expect_eq 1 "$((small < fast))"
}

test::the_archive_being_written_is_not_packed_into_itself() {
  local root members
  root="$(_tree)"
  # Two runs: the first leaves an `out.tar` inside the tree, so the second MEETS it while writing it.
  "$(_xff_bin)" "${root}" -type f --pack="${root}/out.tar"
  "$(_xff_bin)" "${root}" -type f --pack="${root}/out.tar"
  members="$(tar -tf "${root}/out.tar")"
  expect_output_contains "src/a.cc" "${members}"
  expect_output_not_contains "out.tar" "${members}"
}

test::dry_run_reports_what_it_would_pack_and_writes_nothing() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" "${root}/src" -name '*.cc' --pack="${root}/out.tar" --dry-run)"
  expect_output_contains "would pack 2 entries" "${out}"
  expect_eq "0" "$(find "${root}" -maxdepth 1 -name 'out.tar' | wc -l | tr -d ' ')"
}

test::an_existing_output_survives_a_failed_pack() {
  local root status
  root="$(_tree)"
  echo "mine" >"${root}/out.tar.gz"
  status=0
  "$(_xff_bin)" "${root}/src" --pack="${root}/out.tar.gz" --pack-level=99 >/dev/null 2>&1 || status=$?
  expect_eq 2 "${status}"
  expect_eq "mine" "$(cat "${root}/out.tar.gz")"
}

test::a_member_of_another_archive_is_refused_rather_than_dropped() {
  local root status out
  root="$(_tree)"
  COPYFILE_DISABLE=1 tar -c -f "${root}/in.tar" -C "${root}/src" a.cc
  status=0
  out="$("$(_xff_bin)" -z "${root}/in.tar" -type f --pack="${root}/out.tar" 2>&1)" || status=$?
  # 2, the "what you asked for cannot be done" code, not 1: nothing was written and nothing will be.
  expect_eq 2 "${status}"
  expect_output_contains "re-packing members is not supported" "${out}"
  expect_eq "0" "$(find "${root}" -maxdepth 1 -name 'out.tar' | wc -l | tr -d ' ')"
}

test_runner
