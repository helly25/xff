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
