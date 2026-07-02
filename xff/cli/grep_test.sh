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
# End-to-end test of the -grep action (#79): xff prints each content line matching
# a regex as path:line:text (grep's piped form), skips binaries, composes with the
# find predicate set, and is an xff extension the find style rejects.

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

# Runs xff, asserting it exited 0 (surfacing output on a nonzero exit) and echoing
# its output.
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

# Here-string, not `printf | grep`: a pipe lets grep -q's early exit SIGPIPE a
# still-writing printf, which fails under `set -o pipefail` and misreports "no".
_has() { grep -qE -- "$2" <<<"$1" && echo yes || echo no; }

_make_tree() {
  local root
  root="$(mktemp -d)"
  printf 'first TODO line\nsecond line\nanother TODO here\n' >"${root}/a.txt"
  printf 'nothing to match here\n' >"${root}/b.txt"
  printf 'ELF\0hidden TODO\n' >"${root}/c.bin" # a NUL -> binary -> skipped
  echo "${root}"
}

test::grep_prints_path_line_text_for_each_match() {
  local root out
  root="$(_make_tree)"
  out="$(_run "${root}" -type f -grep 'TODO')"
  rm -rf "${root}"
  expect_eq "yes" "$(_has "${out}" '/a\.txt:1:first TODO line$')"
  expect_eq "yes" "$(_has "${out}" '/a\.txt:3:another TODO here$')"
  expect_eq "no" "$(_has "${out}" 'second line')" # a non-matching line is not printed
}

test::grep_uses_regex() {
  local root out
  root="$(_make_tree)"
  out="$(_run "${root}" -type f -grep 'T.DO')" # '.' is a regex wildcard
  rm -rf "${root}"
  expect_eq "yes" "$(_has "${out}" '/a\.txt:1:first TODO line$')"
}

test::grep_skips_binary_files() {
  local root out
  root="$(_make_tree)"
  out="$(_run "${root}" -type f -grep 'TODO')"
  rm -rf "${root}"
  expect_eq "no" "$(_has "${out}" 'c\.bin')" # the binary's TODO is not reported
}

test::grep_composes_with_find_predicates() {
  local root out
  root="$(_make_tree)"
  out="$(_run "${root}" -name 'a.txt' -grep 'TODO')"
  rm -rf "${root}"
  expect_eq "yes" "$(_has "${out}" '/a\.txt:1:first TODO line$')"
  expect_eq "no" "$(_has "${out}" '/b\.txt')"
}

test::grep_is_rejected_by_the_find_style() {
  local root out rc
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --config=find "${root}" -grep 'TODO' 2>&1)" && rc=0 || rc=$?
  rm -rf "${root}"
  expect_eq "2" "${rc}" # an xff extension is a usage error under --config=find
  expect_eq "yes" "$(_has "${out}" 'xff extension')"
}

test_runner
