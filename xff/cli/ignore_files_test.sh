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
# End-to-end test of per-directory ignore files (--ignore-files): .ignore and
# .xffignore are respected only when opted in (find-compatible default is off),
# stack per directory (a deeper file overrides a shallower one), prune directories,
# and are all disabled by -u / --no-ignore. Drives the real binary.

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

# "yes" if $1 (output) has a line ending in the path $2, else "no". Uses `<<<`
# (not `printf | grep`): a pipe lets grep -q's early exit SIGPIPE a still-writing
# printf, which fails under `set -o pipefail` and misreports "no" on large output.
_has() { grep -qE -- "(^|/)${2}\$" <<<"$1" && echo yes || echo no; }

# Tree: top.log, src/main.cc, src/debug.log, build/out.o.
#   <root>/.xffignore      => *.log       (ignore logs everywhere)
#   <root>/src/.xffignore  => !debug.log  (re-include debug.log in src/)
_make_tree() {
  local root
  root="$(mktemp -d)"
  mkdir -p "${root}/src" "${root}/build"
  touch "${root}/top.log" "${root}/src/main.cc" "${root}/src/debug.log" "${root}/build/out.o"
  printf '*.log\n' >"${root}/.xffignore"
  printf '!debug.log\n' >"${root}/src/.xffignore"
  echo "${root}"
}

test::ignore_files_off_by_default() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_eq "yes" "$(_has "${out}" 'top\.log')" # find-compatible: .xffignore not consulted
}

test::ignore_files_honored_when_enabled() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --ignore-files "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_eq "no" "$(_has "${out}" 'top\.log')"  # dropped by the root *.log rule
  expect_eq "yes" "$(_has "${out}" 'main\.cc')" # unaffected
}

test::deeper_ignore_file_overrides_shallower() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --ignore-files "${root}" -type f 2>&1)"
  rm -rf "${root}"
  # src/.xffignore's !debug.log re-includes it even though the root *.log would drop it.
  expect_eq "yes" "$(_has "${out}" 'debug\.log')"
}

test::no_ignore_master_switch_disables() {
  local root out_u out_long
  root="$(_make_tree)"
  out_u="$("$(_xff_bin)" --ignore-files -u "${root}" -type f 2>&1)"
  out_long="$("$(_xff_bin)" --ignore-files --no-ignore "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_eq "yes" "$(_has "${out_u}" 'top\.log')"    # -u forces ignore processing off
  expect_eq "yes" "$(_has "${out_long}" 'top\.log')" # --no-ignore likewise
}

test::ignore_file_prunes_a_directory() {
  local root out
  root="$(mktemp -d)"
  mkdir -p "${root}/keep" "${root}/skip"
  touch "${root}/keep/a" "${root}/skip/b"
  printf 'skip/\n' >"${root}/.xffignore" # directory-only rule
  out="$("$(_xff_bin)" --ignore-files "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_eq "no" "$(_has "${out}" 'skip/b')" # skip/ pruned, its contents gone
  expect_eq "yes" "$(_has "${out}" 'keep/a')"
}

test::dot_ignore_file_is_also_respected() {
  local root out
  root="$(mktemp -d)"
  touch "${root}/a.tmp" "${root}/a.cc"
  printf '*.tmp\n' >"${root}/.ignore" # the generic .ignore, not just .xffignore
  out="$("$(_xff_bin)" --ignore-files "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_eq "no" "$(_has "${out}" 'a\.tmp')"
  expect_eq "yes" "$(_has "${out}" 'a\.cc')"
}

test_runner
