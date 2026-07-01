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
# End-to-end test of -g / --gitignore: per-directory .gitignore files, opt-in (off
# by default, find-compatible), stacked per directory, and disabled by -u.

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

_has() { printf '%s\n' "$1" | grep -qE -- "(^|/)${2}\$" && echo yes || echo no; }

# Tree: keep.cc, a.o, sub/b.o, sub/keep.h.  <root>/.gitignore => *.o
_make_tree() {
  local root
  root="$(mktemp -d)"
  mkdir -p "${root}/sub"
  touch "${root}/keep.cc" "${root}/a.o" "${root}/sub/b.o" "${root}/sub/keep.h"
  printf '*.o\n' >"${root}/.gitignore"
  echo "${root}"
}

test::gitignore_off_by_default() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_eq "yes" "$(_has "${out}" 'a\.o')" # .gitignore not consulted without -g
}

test::dash_g_respects_gitignore_recursively() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" -g "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_eq "no" "$(_has "${out}" 'a\.o')"     # dropped at the root
  expect_eq "no" "$(_has "${out}" 'sub/b\.o')" # and in the subdirectory
  expect_eq "yes" "$(_has "${out}" 'keep\.cc')"
  expect_eq "yes" "$(_has "${out}" 'sub/keep\.h')"
}

test::gitignore_off_value_disables() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --gitignore=off "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_eq "yes" "$(_has "${out}" 'a\.o')"
}

test::no_ignore_master_switch_overrides_dash_g() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" -g -u "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_eq "yes" "$(_has "${out}" 'a\.o')" # -u force-disables even with -g
}

test::nested_gitignore_scopes_to_its_subtree() {
  local root out
  root="$(mktemp -d)"
  mkdir -p "${root}/sub"
  touch "${root}/top.tmp" "${root}/sub/inner.tmp"
  printf '*.tmp\n' >"${root}/sub/.gitignore" # only in sub/
  out="$("$(_xff_bin)" -g "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_eq "no" "$(_has "${out}" 'inner\.tmp')" # sub/.gitignore applies here
  expect_eq "yes" "$(_has "${out}" 'top\.tmp')"  # but not above its directory
}

test::help_topic_documents_gitignore() {
  # Capture the exit status and the full output separately, and surface both when the
  # assertion fails, so a flake (e.g. the sanitizer aborting the subprocess) is
  # diagnosable in CI instead of masquerading as a bare "substring missing".
  local out rc
  out="$("$(_xff_bin)" --help=-g 2>&1)" && rc=0 || rc=$?
  if [[ "${rc}" != "0" ]] || ! printf '%s\n' "${out}" | grep -qi 'gitignore'; then
    echo "help=-g exited ${rc}; captured output:"
    printf '%s\n' "${out}"
  fi
  expect_eq "0" "${rc}"
  expect_eq "yes" "$(printf '%s\n' "${out}" | grep -qi 'gitignore' && echo yes || echo no)"
}

test_runner
