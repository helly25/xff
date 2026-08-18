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
# End-to-end test of -fuzzy / -ifuzzy: the subsequence match, its case rules, and the find-style
# rejection. Runs with --exact throughout where case matters, because the xff default folds case on
# a case-folding volume (APFS / NTFS) and the result would otherwise differ per platform.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${helly25_bashtest}"

# Scratch trees live under bashtest's own ${BASHTEST_TMPDIR}, which its exit trap removes; the name
# is a per-call counter, so a case that builds two trees needs nothing special and no test name
# leaks into a printed path (where it could satisfy an expect_output_not_contains).

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# Names chosen so one tree shows all three answers: a hit through gaps, a hit only when case folds,
# and a name whose letters are present but in the wrong order.
_make_tree() {
  local root
  _xff_tree_seq=$((${_xff_tree_seq:-0} + 1))
  root="${BASHTEST_TMPDIR}/tree${_xff_tree_seq}"
  mkdir -p "${root}"
  : >"${root}/the_main_header.h"
  : >"${root}/README.md"
  : >"${root}/hamster.txt"
  echo "${root}"
}

test::fuzzy_matches_characters_in_order_with_gaps() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --exact "${root}" -type f -fuzzy tmh)"
  expect_output_contains "the_main_header.h" "${out}"
  expect_eq "1" "$(wc -l <<<"${out}" | tr -d ' ')"
}

test::fuzzy_is_not_an_anagram_match() {
  local root out
  root="$(_make_tree)"
  # `hmt` uses the same letters as `tmh` but in another order, and `hamster.txt` holds them in yet
  # another - a subsequence match is about ORDER, which is what separates it from a bag of letters.
  out="$("$(_xff_bin)" --exact "${root}" -type f -fuzzy hmt)"
  expect_output_not_contains "the_main_header.h" "${out}"
}

test::case_follows_exact_and_ifuzzy_always_folds() {
  local root out
  root="$(_make_tree)"
  # Under --exact the letters have to match byte for byte, so lowercase `rdme` misses README.md;
  # -ifuzzy folds regardless.
  out="$("$(_xff_bin)" --exact "${root}" -type f -fuzzy rdme)"
  expect_output_not_contains "README.md" "${out}"
  out="$("$(_xff_bin)" --exact "${root}" -type f -fuzzy RDME)"
  expect_output_contains "README.md" "${out}"
  out="$("$(_xff_bin)" --exact "${root}" -type f -ifuzzy rdme)"
  expect_output_contains "README.md" "${out}"
}

test::fuzzy_matches_the_basename_not_the_whole_path() {
  local root out
  root="$(_make_tree)"
  # The directory components are not part of the subject, so letters that only appear in the leading
  # path do not help - that is what makes -fuzzy the loose counterpart of -name rather than of -path.
  mkdir -p "${root}/zzq"
  : >"${root}/zzq/plain.txt"
  out="$("$(_xff_bin)" --exact "${root}" -type f -fuzzy zzq)"
  expect_output_not_contains "plain.txt" "${out}"
}

test::the_fuzzy_field_ranks_matches_of_the_same_pattern() {
  local root out ranked
  _xff_tree_seq=$((${_xff_tree_seq:-0} + 1))
  root="${BASHTEST_TMPDIR}/tree${_xff_tree_seq}"
  mkdir -p "${root}"
  : >"${root}/tmh.txt"           # the pattern itself: consecutive, at the start
  : >"${root}/the_main_header.h" # three word-start initials
  : >"${root}/themainheader.txt" # the same letters, no word starts
  : >"${root}/automath.hpp"      # buried mid-word
  # The score is only comparable within one pattern, so the assertion is the ORDER a numeric sort
  # puts them in - which is the ranking a user actually gets out of {fuzzy}.
  out="$("$(_xff_bin)" --exact "${root}" -type f -fuzzy tmh --template='{fuzzy} {name}')"
  ranked="$(sort -rn <<<"${out}" | cut -d' ' -f2 | tr '\n' ' ')"
  expect_eq "tmh.txt the_main_header.h themainheader.txt automath.hpp " "${ranked}"
}

test::the_fuzzy_field_is_empty_without_a_fuzzy_test() {
  local root out
  root="$(_make_tree)"
  # Like {shard} outside shard mode: it renders nothing rather than inventing a score.
  out="$("$(_xff_bin)" --exact "${root}" -type f -name 'README.md' --template='[{fuzzy}]')"
  expect_eq "[]" "${out}"
}

test::the_fuzzy_score_does_not_leak_from_one_entry_to_the_next() {
  local root out
  _xff_tree_seq=$((${_xff_tree_seq:-0} + 1))
  root="${BASHTEST_TMPDIR}/tree${_xff_tree_seq}"
  mkdir -p "${root}"
  : >"${root}/tmh.txt"
  : >"${root}/zzz.txt"
  # Both entries are listed (the fuzzy test is one arm of an OR), but only the matching one has a
  # score: a run-scoped slot that was not cleared per entry would give zzz.txt tmh.txt's number.
  out="$("$(_xff_bin)" --exact "${root}" -type f \( -fuzzy tmh -o -name 'zzz.txt' \) --template='{name}=[{fuzzy}]' | sort)"
  expect_output_contains "zzz.txt=[]" "${out}"
  expect_not_matches "zzz.txt=\[[0-9]" "${out}"
}

test::fuzzy_is_an_xff_extension_the_find_style_rejects() {
  local root out rc
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --config=find "${root}" -fuzzy tmh 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "xff extension" "${out}"
}

test::sort_score_ranks_best_match_first() {
  # The point of ranking: the best match leads, whatever order the walk found things in. tmh_exact
  # scores highest (consecutive run at a word start), the_main_header lowest of the three.
  local root out
  _xff_tree_seq=$((${_xff_tree_seq:-0} + 1))
  root="${BASHTEST_TMPDIR}/tree${_xff_tree_seq}"
  mkdir -p "${root}"
  mkdir -p "${root}/src"
  : >"${root}/the_main_header.h"
  : >"${root}/tmp_helper.h"
  : >"${root}/src/tmh_exact.h"
  out="$("$(_xff_bin)" --exact "${root}" -type f -fuzzy tmh --sort=score)"
  # Assert the ORDER, not just membership - membership already held before ranking existed.
  expect_matches "tmh_exact\.h.*tmp_helper\.h.*the_main_header\.h" "${out}"
}

test::sort_score_without_fuzzy_is_a_usage_error() {
  # Ranking by a value nothing produced is a mistake, not an empty ordering.
  local root out rc
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -type f --sort=score 2>&1)" && rc=0 || rc="${?}"
  expect_eq "2" "${rc}"
  # The message NAMES every primary that would satisfy it, so the fix is readable from the error.
  expect_output_contains "needs one of -fuzzy, -fuzzypath, -ifuzzy, -ifuzzypath" "${out}"
}

test::sort_score_refuses_the_formats_it_cannot_reorder() {
  # --format=tree nests by path and aligned/markdown stream through a width buffer; silently
  # ignoring the ranking would be worse than refusing it, and the message names the way out.
  local root out rc
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -fuzzy tmh --format=tree --sort=score 2>&1)" && rc=0 || rc="${?}"
  expect_eq "2" "${rc}"
  expect_output_contains "cannot rank a --format=tree" "${out}"
  expect_output_contains "streaming format" "${out}"
}

test::a_later_sort_mode_turns_ranking_back_off() {
  # --sort is last-wins like every other global; ranking must not be sticky.
  local root out rc
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -type f --sort=score --sort=dir 2>&1)" && rc=0 || rc="${?}"
  expect_eq "0" "${rc}" # no -fuzzy, yet no usage error: ranking was turned off again
}

test::fuzzypath_matches_across_directory_separators() {
  # The whole point of the path variant: `eng/wlk` spans a separator, which no basename match can.
  local root out
  _xff_tree_seq=$((${_xff_tree_seq:-0} + 1))
  root="${BASHTEST_TMPDIR}/tree${_xff_tree_seq}"
  mkdir -p "${root}"
  mkdir -p "${root}/engine" "${root}/docs"
  : >"${root}/engine/walk.cc"
  : >"${root}/engine/run.cc"
  : >"${root}/docs/notes.md"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -fuzzypath "eng/wlk")"
  expect_output_contains "engine/walk.cc" "${out}"
  expect_eq "1" "$(wc -l <<<"${out}" | tr -d ' ')"
  # -fuzzy is the BASENAME, so the same pattern finds nothing.
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -fuzzy "eng/wlk")"
  expect_eq "" "${out}"
}

test::ifuzzypath_folds_case() {
  local root out
  _xff_tree_seq=$((${_xff_tree_seq:-0} + 1))
  root="${BASHTEST_TMPDIR}/tree${_xff_tree_seq}"
  mkdir -p "${root}"
  mkdir -p "${root}/engine"
  : >"${root}/engine/walk.cc"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -ifuzzypath "ENG/WLK")"
  expect_output_contains "engine/walk.cc" "${out}"
}

test::sort_score_accepts_every_scoring_primary() {
  # The ranking gate lists the primaries that SET a score; a new one must not be refused. Without
  # this, adding -fuzzypath left `--sort=score -fuzzypath` erroring out.
  local root out rc
  _xff_tree_seq=$((${_xff_tree_seq:-0} + 1))
  root="${BASHTEST_TMPDIR}/tree${_xff_tree_seq}"
  mkdir -p "${root}"
  mkdir -p "${root}/engine"
  : >"${root}/engine/walk.cc"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -fuzzypath "eng/wlk" --sort=score 2>&1)" && rc=0 || rc="${?}"
  expect_eq "0" "${rc}"
  expect_output_contains "engine/walk.cc" "${out}"
}

test::fuzzypath_is_an_xff_extension_the_find_style_rejects() {
  local root out rc
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --config=find "${root}" -fuzzypath tmh 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "xff extension" "${out}"
}

test_runner
