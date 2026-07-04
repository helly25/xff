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
# Binary-level test of the strict find style: `--config=find` accepts only find's
# own vocabulary and rejects xff extensions (e.g. -println), while the default xff
# style accepts everything. Exercises cli/main.cc's ActiveStyle + EnforceStyle
# wiring end to end (helly25/bashtest).

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

# A directory holding a single file, isolated from any ambient config.
_tree() {
  local dir="${TEST_TMPDIR}/$1"
  mkdir -p "${dir}"
  : >"${dir}/a.txt"
  echo "${dir}"
}

test::strict_find_style_rejects_xff_extensions() {
  local dir
  dir="$(_tree reject)"
  # -println is an xff extension; under --config=find it is rejected before the
  # run starts, with a self-documenting error and exit code 2.
  local out rc
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --config=find "${dir}" -println 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  local lines=()
  local line
  while IFS= read -r line; do lines+=("${line}"); done <<<"${out}"
  expect_contains \
    "xff: '-println' is an xff extension, not available under the find style (--config=find); use --config=xff" \
    "${lines[@]}"
}

test::strict_find_style_accepts_find_vocabulary() {
  local dir
  dir="$(_tree findvocab)"
  # A pure-find expression runs normally under --config=find (exit 0).
  local out rc
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --config=find "${dir}" -name a.txt 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
}

test::xff_style_accepts_xff_extensions() {
  local dir
  dir="$(_tree accept)"
  local xff
  xff="$(_xff_bin)"
  # The default (no --config) modern style runs -println fine.
  local rc
  XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" "${dir}" -name a.txt -println >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  # And explicitly under --config=xff.
  XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=xff "${dir}" -name a.txt -println >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "0" "${rc}"
}

# A tree with a .gitignore and a .ignore, plus a file each would exclude and one kept.
_ignore_tree() {
  local dir="${TEST_TMPDIR}/$1"
  mkdir -p "${dir}/build"
  printf 'build/\n' >"${dir}/.gitignore"
  printf '*.tmp\n' >"${dir}/.ignore"
  : >"${dir}/build/out.o" # excluded by .gitignore
  : >"${dir}/junk.tmp"    # excluded by .ignore
  : >"${dir}/keep.txt"    # kept
  echo "${dir}"
}

test::rg_style_respects_ignore_files_by_default() {
  local dir out
  dir="$(_ignore_tree rgignore)"
  # --config=rg turns on .gitignore + .ignore with no flags (ripgrep's headline default).
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --config=rg "${dir}" -type f 2>&1)"
  expect_output_contains "keep.txt" "${out}"
  expect_output_not_contains "out.o" "${out}"    # .gitignore build/
  expect_output_not_contains "junk.tmp" "${out}" # .ignore *.tmp
  # The find style leaves ignore files off, so it sees everything.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --config=find "${dir}" -type f 2>&1)"
  expect_output_contains "out.o" "${out}"
  expect_output_contains "junk.tmp" "${out}"
}

test::rg_style_accepts_xff_extensions() {
  local dir rc
  dir="$(_tree rgvocab)"
  # rg uses the full xff vocabulary; only the strict find style rejects extensions.
  XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --config=rg "${dir}" -name a.txt -println >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "0" "${rc}"
}

test::xfd_style_respects_ignore_files_by_default() {
  local dir out
  dir="$(_ignore_tree xfdignore)"
  # --config=xfd (the fd-like flavor) turns on .gitignore + .ignore by default, like rg.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --config=xfd "${dir}" -type f 2>&1)"
  expect_output_contains "keep.txt" "${out}"
  expect_output_not_contains "out.o" "${out}"    # .gitignore build/
  expect_output_not_contains "junk.tmp" "${out}" # .ignore *.tmp
}

test::argv0_fd_alias_defaults_to_xfd_style() {
  local dir out
  dir="$(_ignore_tree argv0fd)"
  # Invoked through an `fd`-named symlink, the opinionated xfd style is the default (no
  # --config needed): ignore files are respected.
  ln -sf "$(_xff_bin)" "${TEST_TMPDIR}/fd"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${TEST_TMPDIR}/fd" "${dir}" -type f 2>&1)"
  expect_output_contains "keep.txt" "${out}"
  expect_output_not_contains "out.o" "${out}"
}

# A tree with a hidden dotfile alongside a visible one.
_hidden_tree() {
  local dir="${TEST_TMPDIR}/$1"
  mkdir -p "${dir}"
  : >"${dir}/.secret"
  : >"${dir}/visible.txt"
  echo "${dir}"
}

test::hidden_dotfiles_skipped_by_opinionated_styles() {
  local dir out xff
  dir="$(_hidden_tree hid)"
  xff="$(_xff_bin)"
  # find / xff (conservative) show dotfiles; xfd / rg (opinionated) skip them.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=xff "${dir}" -type f 2>&1)"
  expect_output_contains ".secret" "${out}"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=xfd "${dir}" -type f 2>&1)"
  expect_output_contains "visible.txt" "${out}"
  expect_output_not_contains ".secret" "${out}"
  # --hidden opts the opinionated style back in; --no-hidden opts the conservative out.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=xfd --hidden "${dir}" -type f 2>&1)"
  expect_output_contains ".secret" "${out}"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=xff --no-hidden "${dir}" -type f 2>&1)"
  expect_output_not_contains ".secret" "${out}"
}

test::case_smart_and_overrides() {
  local dir out xff
  dir="${TEST_TMPDIR}/casesmart"
  mkdir -p "${dir}"
  : >"${dir}/README.md"
  xff="$(_xff_bin)"
  # Use the find style for globs (no FS-native folding), so the case flags are the only
  # case input on any platform. smart: an all-lowercase pattern folds; one with an
  # uppercase letter stays exact.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=find --case=smart "${dir}" -name 'readme*' 2>&1)"
  expect_output_contains "README.md" "${out}"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=find --case=smart "${dir}" -name 'Readme*' 2>&1)"
  expect_output_not_contains "README.md" "${out}" # uppercase in pattern -> exact
  # sensitive (find default): a lowercase pattern does not match the uppercase file.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=find "${dir}" -name 'readme*' 2>&1)"
  expect_output_not_contains "README.md" "${out}"
  # -i forces insensitive.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=find -i "${dir}" -name 'readme*' 2>&1)"
  expect_output_contains "README.md" "${out}"
  # xfd defaults to smart; a lowercase regex folds (regex ignores FS-native, so portable),
  # and -s- forces sensitive back off.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=xfd "${dir}" -regex '.*readme.*' 2>&1)"
  expect_output_contains "README.md" "${out}"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=xfd -s- "${dir}" -regex '.*readme.*' 2>&1)"
  expect_output_not_contains "README.md" "${out}"
}

test::help_styles_shows_the_flavor_comparison() {
  local out
  out="$("$(_xff_bin)" --help=styles 2>&1)"
  # All four style columns and the key behavior rows are present.
  expect_output_contains "xfd" "${out}"
  expect_output_contains "hidden dotfiles" "${out}"
  expect_output_contains "letter case" "${out}"
  # hidden: find/xff show, xfd/rg skip (one row, so the whole-text regex stays on that line).
  expect_matches 'hidden dotfiles.*show.*show.*skip.*skip' "${out}"
}

test::explain_adds_the_current_flavor_column() {
  local out
  # --explain prints the flavor table with a `current` column resolved for this run.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --config=rg --explain 2>&1)"
  expect_output_contains "current" "${out}"
  expect_matches 'letter case.*smart' "${out}" # rg resolves case -> smart
}

test::argv0_find_alias_defaults_to_strict_style() {
  local dir
  dir="$(_tree argv0)"
  # Invoked through a `find`-named symlink, the strict find style is the default
  # (no --config needed): an xff extension is rejected with exit 2.
  ln -sf "$(_xff_bin)" "${TEST_TMPDIR}/find"
  local out rc
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${TEST_TMPDIR}/find" "${dir}" -println 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  local lines=()
  local line
  while IFS= read -r line; do lines+=("${line}"); done <<<"${out}"
  expect_contains \
    "xff: '-println' is an xff extension, not available under the find style (--config=find); use --config=xff" \
    "${lines[@]}"
}

test::argv0_xff_alias_defaults_to_modern_style() {
  local dir
  dir="$(_tree argv0xff)"
  # Invoked as xff (its real name), the modern style is the default: -println runs.
  local rc
  XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name a.txt -println >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "0" "${rc}"
}

test_runner
