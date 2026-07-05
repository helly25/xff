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
# End-to-end test of -diff (via mbo::diff): a unified diff by default, the -diff=STYLE output
# formats, TRUE = same polarity (silent when equal), a binary side noted on stderr, and the
# --diff-algorithm usage errors. Drives the real binary (the diff is emitted by the action).

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

# The per-mode output (u/c/n/y/none) is golden-tested by the `diff_golden` targets: one
# committed expected-output file per mode, checked with mbo's diff_test. This bashtest covers
# what is not stdout-golden-able -- the TRUE = same polarity, --diff-algorithm acceptance, the
# binary stderr note, and the usage errors.

test::diff_polarity_true_when_equal_false_when_different() {
  local dir out
  dir="${TEST_TMPDIR}/diffpol"
  mkdir -p "${dir}"
  printf 'one\ntwo\n' >"${dir}/a.txt"
  printf 'one\nTWO\n' >"${dir}/b.txt"
  printf 'one\ntwo\n' >"${dir}/same.txt"
  # Equal -> -diff is silent (no diff) but TRUE, so a trailing -print fires.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name a.txt -diff "${dir}/same.txt" -print 2>&1)"
  expect_output_not_contains '@@' "${out}"
  expect_output_contains 'a.txt' "${out}"
  # Different -> -diff=none is the silent matcher and FALSE, so -print does not fire.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name a.txt -diff=none "${dir}/b.txt" -print 2>&1)"
  expect_output_not_contains 'a.txt' "${out}"
  # A valid --diff-algorithm is accepted and still produces the diff.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --diff-algorithm=naive "${dir}" -name a.txt -diff "${dir}/b.txt" 2>&1)"
  expect_output_contains '+TWO' "${out}"
}

test::diff_binary_notes_on_stderr_and_bad_inputs_are_usage_errors() {
  local dir out rc
  dir="${TEST_TMPDIR}/diffbin"
  mkdir -p "${dir}"
  printf 'x\000one' >"${dir}/p.bin" # embedded NUL -> binary
  printf 'x\000two' >"${dir}/q.bin"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name p.bin -diff "${dir}/q.bin" 2>&1)"
  expect_output_contains 'Binary files' "${out}" # byte-compared, not text-diffed
  # A bad -diff=STYLE is a usage error.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name p.bin -diff=zz "${dir}/q.bin" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains 'unknown style' "${out}"
  # A bad --diff-algorithm is a usage error.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --diff-algorithm=bogus "${dir}" -name p.bin -diff "${dir}/q.bin" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains 'unknown diff algorithm' "${out}"
}

test_runner
