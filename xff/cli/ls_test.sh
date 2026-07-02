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
# End-to-end test of -ls column alignment and the --buffer modes: the columns line
# up (via a buffered ColumnBuffer), and off/all/N are accepted. Drives the binary.

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

# Runs xff asserting exit 0, surfacing output on failure (so a sanitizer abort is
# diagnosable rather than a bare empty-output miss). Echoes the captured output.
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

# `<<<` (not `printf | grep`): a pipe lets grep -q's early exit SIGPIPE a
# still-writing printf, which fails under `set -o pipefail` and misreports "no".
_has() { grep -qE -- "$2" <<<"$1" && echo yes || echo no; }

# Two files with very different size widths (1 vs 6 digits) and name lengths.
_make_tree() {
  local root
  root="$(mktemp -d)"
  head -c 1 /dev/zero >"${root}/aaa"
  head -c 100000 /dev/zero >"${root}/b"
  echo "${root}"
}

test::ls_lists_entries_with_perms() {
  local root out
  root="$(_make_tree)"
  out="$(_run "${root}" -type f -ls)"
  rm -rf "${root}"
  expect_eq "yes" "$(_has "${out}" 'aaa')"
  expect_eq "yes" "$(_has "${out}" '\-rw')" # the permission column is present
}

test::ls_columns_are_aligned() {
  local root out plen_a plen_b
  root="$(_make_tree)"
  out="$(_run "${root}" -type f -ls)"
  rm -rf "${root}"
  # The path is the last field; when the columns are aligned, the fixed prefix before
  # the path is the same width on every row regardless of size or name length.
  plen_a="$(printf '%s\n' "${out}" | awk '$NF ~ /aaa$/ { print length($0) - length($NF) }')"
  plen_b="$(printf '%s\n' "${out}" | awk '$NF ~ /\/b$/  { print length($0) - length($NF) }')"
  expect_eq "${plen_a}" "${plen_b}"
}

test::buffer_modes_are_accepted() {
  local root
  root="$(_make_tree)"
  # off (min widths), all (full buffering), and an explicit window all run and list.
  expect_eq "yes" "$(_has "$(_run --buffer=off "${root}" -type f -ls)" 'aaa')"
  expect_eq "yes" "$(_has "$(_run --buffer=all "${root}" -type f -ls)" 'aaa')"
  expect_eq "yes" "$(_has "$(_run --buffer=1 "${root}" -type f -ls)" 'aaa')"
  rm -rf "${root}"
}

test::help_topic_documents_buffer() {
  expect_eq "yes" "$(_has "$(_run --help=--buffer)" 'buffer')"
}

test_runner
