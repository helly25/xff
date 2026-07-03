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

# A real newline for line-anchored expect_matches patterns: bashtest's expect_matches
# matches the whole text ([[ =~ ]]), so `^`/`$` anchor the whole output, not a line.
# `(^|${NL})X` and `X($|${NL})` restore grep's per-line anchoring (a real newline,
# because `\n` is not a portable ERE escape).
NL=$'\n'

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
  expect_matches "/a\.txt:1:first TODO line(\$|${NL})" "${out}"
  expect_matches "/a\.txt:3:another TODO here(\$|${NL})" "${out}"
  expect_not_matches 'second line' "${out}" # a non-matching line is not printed
}

test::grep_uses_regex() {
  local root out
  root="$(_make_tree)"
  out="$(_run "${root}" -type f -grep 'T.DO')" # '.' is a regex wildcard
  rm -rf "${root}"
  expect_matches "/a\.txt:1:first TODO line(\$|${NL})" "${out}"
}

test::grep_skips_binary_files() {
  local root out
  root="$(_make_tree)"
  out="$(_run "${root}" -type f -grep 'TODO')"
  rm -rf "${root}"
  expect_not_matches 'c\.bin' "${out}" # the binary's TODO is not reported
}

test::grep_composes_with_find_predicates() {
  local root out
  root="$(_make_tree)"
  out="$(_run "${root}" -name 'a.txt' -grep 'TODO')"
  rm -rf "${root}"
  expect_matches "/a\.txt:1:first TODO line(\$|${NL})" "${out}"
  expect_not_matches '/b\.txt' "${out}"
}

test::grep_is_rejected_by_the_find_style() {
  local root out rc
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --config=find "${root}" -grep 'TODO' 2>&1)" && rc=0 || rc=$?
  rm -rf "${root}"
  expect_eq "2" "${rc}" # an xff extension is a usage error under --config=find
  expect_matches 'xff extension' "${out}"
}

test::regextype_exact_matches_literally() {
  local root out
  root="$(mktemp -d)"
  printf 'price 3.50\nprice 3X50\n' >"${root}/p.txt"
  out="$(_run --regextype=EXACT "${root}" -type f -grep '3.50')" # '.' is literal under EXACT
  rm -rf "${root}"
  expect_matches "/p\.txt:1:price 3\.50(\$|${NL})" "${out}"
  expect_not_matches '3X50' "${out}" # the regex-wildcard match is gone
}

test::regextype_reserved_value_is_a_usage_error() {
  local root out rc
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --regextype=MATCH "${root}" -grep 'TODO' 2>&1)" && rc=0 || rc=$?
  rm -rf "${root}"
  expect_eq "2" "${rc}" # MATCH / PCRE are reserved (#85)
  expect_matches 'not supported yet' "${out}"
}

test::grep_format_overrides_the_default_output() {
  local root out
  root="$(_make_tree)"
  # -grep=FORMAT renders a field template ({line}/{text} + the entry vocabulary).
  out="$(_run "${root}" -name 'a.txt' -grep='{line}|{text}' 'TODO')"
  rm -rf "${root}"
  expect_matches "(^|${NL})1\|first TODO line(\$|${NL})" "${out}"
  expect_matches "(^|${NL})3\|another TODO here(\$|${NL})" "${out}"
  expect_not_matches ':' "${out}" # the default path:line:text colons are gone
}

test::grep_match_field_extracts_only_the_match() {
  local root out
  root="$(mktemp -d)"
  printf 'error E42 here\nwarn E7\n' >"${root}/log.txt"
  # {match} is grep -o: just the matched substring, {column} its 1-based start.
  out="$(_run "${root}" -name 'log.txt' -grep='{column}:{match}' 'E[0-9]+')"
  rm -rf "${root}"
  expect_matches "(^|${NL})7:E42(\$|${NL})" "${out}" # E42 at column 7 of "error E42 here"
  expect_matches "(^|${NL})6:E7(\$|${NL})" "${out}"  # E7 at column 6 of "warn E7"
}

test::grep_count_prints_per_file_match_line_count() {
  local root out
  root="$(mktemp -d)"
  printf 'TODO 1\nx\nTODO 2\n' >"${root}/a.txt"
  printf 'no hits\n' >"${root}/b.txt"
  # --count (a leading global): path:count per file with matches; 0-match files omitted.
  out="$(_run --count "${root}" -type f -grep 'TODO')"
  rm -rf "${root}"
  expect_matches "/a\.txt:2(\$|${NL})" "${out}"
  expect_not_matches 'b\.txt' "${out}" # no matches -> not listed
  expect_not_matches 'TODO' "${out}"   # the lines themselves are not printed
}

test_runner
