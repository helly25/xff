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
# End-to-end test of --summary output: a right-aligned human table by default
# (grouped digits) and stable one-object-per-row --format=jsonl machine output.

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

# Runs xff and returns its combined output, asserting it exited 0 (surfacing the
# output on a nonzero exit so a sanitizer abort is diagnosable rather than hidden).
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

_has() { printf '%s\n' "$1" | grep -qE -- "$2" && echo yes || echo no; }

# Tree: a.txt (1234 bytes), b.txt (10 bytes), c.md (5 bytes).
_make_tree() {
  local root
  root="$(mktemp -d)"
  head -c 1234 /dev/zero >"${root}/a.txt"
  head -c 10 /dev/zero >"${root}/b.txt"
  head -c 5 /dev/zero >"${root}/c.md"
  echo "${root}"
}

test::summary_default_is_human_and_right_aligned_in_xff() {
  local root out
  root="$(_make_tree)"
  out="$(_run --summary=ext "${root}" -type f)"
  rm -rf "${root}"
  # xff style defaults to human sizes: txt (2 files, 1244 bytes) -> KiB, md (5) -> B,
  # with the count right of the label.
  expect_eq "yes" "$(_has "${out}" 'txt +2 +[0-9.]+ KiB')"
  expect_eq "yes" "$(_has "${out}" 'total +3 +[0-9.]+ KiB')"
  expect_eq "yes" "$(_has "${out}" 'md +1 +5 B')"
}

test::summary_human_off_shows_grouped_bytes() {
  local root out
  root="$(_make_tree)"
  out="$(_run --summary=ext --human=off "${root}" -type f)"
  rm -rf "${root}"
  # --human=off forces raw grouped bytes (the machine-ish view), right-aligned.
  expect_eq "yes" "$(_has "${out}" 'txt +2 +1,244')"
  expect_eq "yes" "$(_has "${out}" 'total +3 +1,249')"
}

test::summary_jsonl_emits_one_object_per_row() {
  local root out
  root="$(_make_tree)"
  out="$(_run --summary=ext --format=jsonl "${root}" -type f)"
  rm -rf "${root}"
  expect_eq "yes" "$(_has "${out}" '\{"group":"txt","count":2,"bytes":1244\}')"
  expect_eq "yes" "$(_has "${out}" '\{"group":"total","count":3,"bytes":1249\}')"
}

test::summary_overall_is_a_single_total_row() {
  local root out
  root="$(_make_tree)"
  out="$(_run --summary --format=jsonl "${root}" -type f)"
  rm -rf "${root}"
  expect_eq "yes" "$(_has "${out}" '\{"group":"total","count":3,"bytes":1249\}')"
}

# A 5,872,025-byte file to exercise human size units (5.6 MiB / 5.9 MB).
_make_big() {
  local root
  root="$(mktemp -d)"
  head -c 5872025 /dev/zero >"${root}/big.bin"
  echo "${root}"
}

test::human_iec_renders_binary_units() {
  local root out
  root="$(_make_big)"
  out="$(_run --summary --human "${root}" -type f)"
  rm -rf "${root}"
  expect_eq "yes" "$(_has "${out}" '5\.6 MiB')" # bare --human = iec (binary)
}

test::human_si_renders_decimal_units() {
  local root out
  root="$(_make_big)"
  out="$(_run --summary --human=si "${root}" -type f)"
  rm -rf "${root}"
  expect_eq "yes" "$(_has "${out}" '5\.9 MB')"
}

test::human_does_not_change_jsonl_bytes() {
  # jsonl is the machine path: exact bytes regardless of --human.
  local root out
  root="$(_make_big)"
  out="$(_run --summary --human --format=jsonl "${root}" -type f)"
  rm -rf "${root}"
  expect_eq "yes" "$(_has "${out}" '"bytes":5872025')"
}

test_runner
