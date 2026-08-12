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
    expect_output_contains "${root}/b.txt" "${out}" # an ordinary file is untouched by diving
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
  expect_eq "2" "$(wc -l <<<"${out}" | tr -d ' ')"
  rm -rf "${root}"
}

test::content_predicates_read_a_members_own_bytes() {
  local root out
  root="$(_tree)"
  # The payoff of member reads: -content and -grep search INSIDE the archive, and only the member
  # that holds the needle matches (the container is a tar, so it never matches as text).
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar" -content needle)"
  expect_output_contains "a.tar!one.txt" "${out}"
  expect_eq "1" "$(wc -l <<<"${out}" | tr -d ' ')" # and nothing else in the archive holds it
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar" -grep needle)"
  expect_output_contains "a.tar!one.txt:1:needle" "${out}"
  rm -rf "${root}"
}

test::hash_and_line_fields_render_for_a_member() {
  local root member plain
  root="$(_tree)"
  # A member's digest must be the digest of its CONTENT, so it equals the digest of a loose file
  # holding the same bytes - the check a manifest (`-hasheq`) over an archive depends on.
  echo "needle" >"${root}/copy.txt"
  member="$("$(_xff_bin)" --archive=roots "${root}/a.tar" -name 'one.txt' -printfln '%{hash} %{lines}')"
  plain="$("$(_xff_bin)" "${root}/copy.txt" -printfln '%{hash} %{lines}')"
  expect_eq "${plain}" "${member}"
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

test::archive_depth_bounds_nesting() {
  local root out
  root="$(_tree)"
  # A tar holding a tar. The default depth of 1 leaves the inner one a plain member; --archive-depth=2
  # opens it, and its member is then an ordinary entry the expression reads like any other.
  mkdir -p "${root}/nest"
  COPYFILE_DISABLE=1 tar -c -f "${root}/nest/inner.tar" -C "${root}" a.tar
  COPYFILE_DISABLE=1 tar -c -f "${root}/outer.tar" -C "${root}/nest" inner.tar
  out="$("$(_xff_bin)" --archive=all "${root}/outer.tar")"
  expect_output_contains "outer.tar!inner.tar" "${out}"
  expect_output_not_contains "outer.tar!inner.tar!" "${out}"
  out="$("$(_xff_bin)" --archive=all --archive-depth=3 "${root}/outer.tar")"
  expect_output_contains "outer.tar!inner.tar!a.tar" "${out}"
  # Three containers deep, the innermost member is still just an entry with readable content.
  out="$("$(_xff_bin)" --archive=all --archive-depth=3 "${root}/outer.tar" -grep needle)"
  expect_output_contains "outer.tar!inner.tar!a.tar!one.txt:1:needle" "${out}"
  rm -rf "${root}"
}

test::a_bad_archive_depth_is_a_usage_error() {
  local root out rc
  root="$(_tree)"
  # 0 most likely means "off", which --archive=none spells; guessing would be worse than saying so.
  out="$("$(_xff_bin)" --archive=all --archive-depth=0 "${root}" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "--archive-depth" "${out}"
  out="$("$(_xff_bin)" --archive=all --archive-depth=lots "${root}" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  rm -rf "${root}"
}

test::all_only_opens_files_whose_name_looks_like_a_container() {
  local root out
  root="$(_tree)"
  # The gate: `blob` IS a tar, byte for byte, but nothing in its name says so. Under `all` the walk
  # leaves it closed - otherwise every file in a source tree would be opened and format-bid - while the
  # `.tar` beside it is dived as before.
  cp "${root}/a.tar" "${root}/blob"
  out="$("$(_xff_bin)" --archive=all "${root}")"
  expect_output_contains "a.tar!one.txt" "${out}"
  expect_output_contains "${root}/blob" "${out}"
  expect_output_not_contains "blob!" "${out}"
  rm -rf "${root}"
}

test::archive_any_offers_every_file_to_the_reader() {
  local root out
  root="$(_tree)"
  # The way out of the name gate, for a tree of archives named anything at all.
  cp "${root}/a.tar" "${root}/blob"
  out="$("$(_xff_bin)" --archive=all --archive-any "${root}")"
  expect_output_contains "blob!one.txt" "${out}"
  # And an ordinary file offered to the reader is still just a file, not an error.
  expect_output_contains "${root}/b.txt" "${out}"
  expect_output_not_contains "xff:" "${out}"
  rm -rf "${root}"
}

test::a_container_named_on_the_command_line_is_never_gated_by_its_name() {
  local root out
  root="$(_tree)"
  # Pointing xff AT a file is the request to look inside it, so `roots` (and `all`) open it whatever it
  # is called - the gate exists for files the walk merely met.
  cp "${root}/a.tar" "${root}/blob"
  out="$("$(_xff_bin)" --archive=roots "${root}/blob")"
  expect_output_contains "blob!one.txt" "${out}"
  out="$("$(_xff_bin)" --archive=all "${root}/blob")"
  expect_output_contains "blob!one.txt" "${out}"
  rm -rf "${root}"
}

test::a_compressed_single_file_dives_to_the_name_inside() {
  local root out
  root="$(_tree)"
  # `notes.txt.gz` is not an archive of many members: it is ONE file, compressed. Its member takes the
  # container's name minus the suffix - what `gzip -d` restores - and content search reads it.
  printf 'findable-needle\n' >"${root}/notes.txt"
  gzip -c "${root}/notes.txt" >"${root}/notes.txt.gz"
  out="$("$(_xff_bin)" --archive=roots "${root}/notes.txt.gz")"
  expect_output_contains "notes.txt.gz!notes.txt" "${out}"
  out="$("$(_xff_bin)" --archive=roots "${root}/notes.txt.gz" -grep findable-needle)"
  expect_output_contains "notes.txt.gz!notes.txt:1:findable-needle" "${out}"
  # And the MEMBER's size is the uncompressed one, since that is what the entry holds - the container
  # keeps its own (compressed) size, so both are listed and only the member's is checked here.
  out="$("$(_xff_bin)" --archive=roots "${root}/notes.txt.gz" -name 'notes.txt' -printfln '%s')"
  expect_eq "16" "${out}"
  rm -rf "${root}"
}

test::a_text_file_named_gz_is_not_a_container() {
  local root out
  root="$(_tree)"
  # The guard on the guard: libarchive's `raw` format bids on anything, so a name claiming compression
  # is not enough - a real codec must have applied. Otherwise every mis-named text file would present as
  # a one-member archive.
  printf 'just text, no gzip header\n' >"${root}/liar.gz"
  out="$("$(_xff_bin)" --archive=roots "${root}/liar.gz")"
  expect_output_contains "${root}/liar.gz" "${out}"
  expect_output_not_contains "liar.gz!" "${out}"
  rm -rf "${root}"
}

test::a_tar_gz_is_still_read_as_a_tar() {
  local root out
  root="$(_tree)"
  # The distinction that matters: `.tar.gz` is a compressed ARCHIVE (libarchive reads it whole, members
  # and all), while `.txt.gz` is a compressed FILE. Both end in `.gz`.
  mkdir -p "${root}/pack"
  printf 'inner\n' >"${root}/pack/inner.txt"
  COPYFILE_DISABLE=1 tar -czf "${root}/pack.tar.gz" -C "${root}/pack" inner.txt
  out="$("$(_xff_bin)" --archive=roots "${root}/pack.tar.gz")"
  expect_output_contains "pack.tar.gz!inner.txt" "${out}"
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
