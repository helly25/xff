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
# Dual-binary parity: the lean `xff` and the extended `xff_full` are thin shims over the same
# main_cc, differing only by the composable extras they link and the `_full` argv[0]
# (DefaultStyleForProgram strips it, so both resolve to the xff style). So any command that uses NO
# extension feature must behave byte-identically in both. This runs a representative set of core
# commands through both binaries and asserts identical stdout + exit code - a guard against the two
# ever diverging on shared code (e.g. a stray `#ifdef` keyed on an extra being linked).
#
# XFF_FULL_ONLY (it needs xff_full), so it runs in every full-mode CI job and is skipped in a lean
# build. Cases deliberately avoid (a) anything that prints the program name - errors, --help,
# --version - which legitimately differ ("xff" vs "xff_full"), and (b) any extension-gated feature
# (--regextype=PCRE2, --archive). `--sort=tree` keeps the traversal order deterministic.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${helly25_bashtest}"

# Resolve the lean `xff` ($1=xff) or extended `xff_full` ($1=xff_full) runfile.
_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/$1"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name "$1" -path "*xff/cli/$1" 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# A small deterministic tree the parity cases run against.
_make_tree() {
  local root
  root="$(mktemp -d)"
  mkdir -p "${root}/sub"
  printf 'alpha\n' >"${root}/a.txt"
  printf 'beta beta\n' >"${root}/sub/b.log"
  : >"${root}/sub/c.txt"
  echo "${root}"
}

# Assert `xff <args>` and `xff_full <args>` produce identical stdout+stderr and exit code.
expect_parity() {
  local lean full lrc frc
  lean="$("$(_bin xff)" "$@" 2>&1)" && lrc=0 || lrc=$?
  full="$("$(_bin xff_full)" "$@" 2>&1)" && frc=0 || frc=$?
  expect_eq "${lrc}" "${frc}"
  expect_eq "${lean}" "${full}"
}

test::xff_and_xff_full_agree_on_core_commands() {
  local root
  root="$(_make_tree)"
  expect_parity "${root}" --sort=tree                                        # bare walk
  expect_parity "${root}" --sort=tree -type f                                # a test
  expect_parity "${root}" --sort=tree -name '*.txt'                          # glob name match
  expect_parity "${root}" --sort=tree -type f -size +0c                      # size compare
  expect_parity "${root}" --sort=tree -regex '.*\.txt'                       # RE2 (core, in both)
  expect_parity "${root}" --sort=tree -type f -printf '%p %s\n'              # printf action
  expect_parity "${root}" --sort=tree '(' -name '*.txt' -o -name '*.log' ')' # operators
  expect_parity "${root}" --summary=type                                     # a reduction
  rm -rf "${root}"
}

test_runner
