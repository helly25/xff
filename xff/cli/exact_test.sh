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
# End-to-end test of --exact / FS-native name matching (#45): the xff style
# matches -name the way the entry's own volume resolves names (folding case on a
# case-insensitive volume), --exact forces verbatim byte-exact matching, and the
# find style stays byte-exact. The FS-native case is gated on a runtime probe of
# the test volume, so it is correct on both case-sensitive (Linux) and
# case-folding (macOS default) runners.

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

# Runs xff, asserting it exited 0 (surfacing output on a nonzero exit so a
# sanitizer abort is diagnosable rather than hidden) and returning its output.
_run() {
  local out rc
  out="$("$(_xff_bin)" "$@" 2>&1)" && rc=0 || rc=$?
  if [[ "${rc}" != "0" ]]; then
    echo "xff $* exited ${rc}; output:"
    printf '%s\n' "${out}"
  fi
  expect_eq "0" "${rc}"
  printf '%s' "${out}"
}

# A tree with one mixed-case file, Foo.txt.
_make_tree() {
  local root
  root="$(mktemp -d)"
  : >"${root}/Foo.txt"
  echo "${root}"
}

# "yes" if the current temp volume folds case (FOO resolves to a lower-case foo),
# else "no" -- so the FS-native assertion below matches whichever runner we are on.
_volume_folds_case() {
  local d
  d="$(mktemp -d)"
  : >"${d}/probe"
  local folds="no"
  [[ -e "${d}/PROBE" ]] && folds="yes"
  rm -rf "${d}"
  echo "${folds}"
}

test::exact_forces_byte_exact_matching() {
  local root out
  root="$(_make_tree)"
  # --exact: verbatim byte comparison regardless of the volume.
  out="$(_run --exact "${root}" -name Foo.txt)"
  expect_matches 'Foo\.txt' "${out}" # exact-case name matches
  out="$(_run --exact "${root}" -name foo.txt)"
  expect_not_matches 'Foo\.txt' "${out}" # wrong-case name does not
  rm -rf "${root}"
}

test::find_style_is_byte_exact() {
  local root out
  root="$(_make_tree)"
  # The find style is drop-in faithful: no FS-native folding.
  out="$(_run --config=find "${root}" -name foo.txt)"
  expect_not_matches 'Foo\.txt' "${out}"
  rm -rf "${root}"
}

test::xff_default_follows_volume_case() {
  local root out
  root="$(_make_tree)"
  # Bare xff style, no --exact: -name matches the way the volume resolves names.
  out="$(_run "${root}" -name foo.txt)"
  if [[ "$(_volume_folds_case)" == "yes" ]]; then
    expect_matches 'Foo\.txt' "${out}"
  else
    expect_not_matches 'Foo\.txt' "${out}"
  fi
  rm -rf "${root}"
}

test::exact_case_name_always_matches_in_xff() {
  local root out
  root="$(_make_tree)"
  # Folding only widens what matches; the verbatim name always matches.
  out="$(_run "${root}" -name Foo.txt)"
  expect_matches 'Foo\.txt' "${out}"
  rm -rf "${root}"
}

test_runner
