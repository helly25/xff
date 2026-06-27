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
# Binary-level test of -exec, which spawns real child processes: the `+` batch
# form runs the command once with all matches appended, while `\;` runs it once
# per match. Drives the real xff binary end to end (helly25/bashtest). The child
# appends a RUN marker per invocation (via $OUT, inherited from xff's env) plus
# each path, so counting RUN lines distinguishes one batched call from N calls.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${helly25_bashtest}"

# Path to the built xff binary in the test runfiles.
_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# A directory with two .txt files (matches) and one non-match, isolated from any
# ambient config (XFF_CONFIG points at a nonexistent path in each run).
_tree() {
  local dir="${TEST_TMPDIR}/$1"
  mkdir -p "${dir}"
  : >"${dir}/a.txt"
  : >"${dir}/b.txt"
  : >"${dir}/skip.md"
  echo "${dir}"
}

# Reads the lines of file $1 into the global array `_lines` (no mapfile; the
# macOS system bash is 3.2).
_read_lines() {
  _lines=()
  local line
  while IFS= read -r line; do _lines+=("${line}"); done <"$1"
}

test::exec_plus_runs_once_with_all_matches() {
  local dir out
  dir="$(_tree plus)"
  out="${TEST_TMPDIR}/plus.out"
  : >"${out}"
  # shellcheck disable=SC2016  # the single-quoted script runs in the spawned sh, which expands $OUT/$@/$p
  XFF_CONFIG="${TEST_TMPDIR}/none" OUT="${out}" "$(_xff_bin)" "${dir}" -name '*.txt' \
    -exec sh -c 'echo RUN >>"$OUT"; for p in "$@"; do echo "$p" >>"$OUT"; done' _ '{}' +
  _read_lines "${out}"
  # One RUN marker (a single batched invocation) plus the two matched paths.
  expect_eq "3" "${#_lines[@]}"
  local runs=0 line
  for line in "${_lines[@]}"; do
    [[ "${line}" == "RUN" ]] && runs=$((runs + 1))
  done
  expect_eq "1" "${runs}"
  expect_contains "${dir}/a.txt" "${_lines[@]}"
  expect_contains "${dir}/b.txt" "${_lines[@]}"
}

test::exec_semicolon_runs_once_per_match() {
  local dir out
  dir="$(_tree semi)"
  out="${TEST_TMPDIR}/semi.out"
  : >"${out}"
  # shellcheck disable=SC2016  # the single-quoted script runs in the spawned sh, which expands $OUT
  XFF_CONFIG="${TEST_TMPDIR}/none" OUT="${out}" "$(_xff_bin)" "${dir}" -name '*.txt' \
    -exec sh -c 'echo RUN >>"$OUT"' \;
  _read_lines "${out}"
  # Two matches, the `\;` form spawns the command once per match: two RUN lines.
  expect_eq "2" "${#_lines[@]}"
}

test_runner
