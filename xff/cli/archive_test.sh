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
# End-to-end test of the --archive / -z control surface: every spelling of "give me
# archive handling" is a hard, self-documenting error in a build without the archive
# extra, while every spelling of "keep find's behavior" is accepted and walks normally.
# The diving itself is not implemented yet; this pins the surface and the two distinct
# error states so neither can regress into a silent no-op.

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

# The scratch tree lives under bashtest's own ${BASHTEST_TMPDIR}, which its exit trap removes, so
# no case repeats a mktemp/rm pair (and a case that fails an expectation no longer leaks its
# tree, because the rm never ran). The name is a COUNTER rather than the calling test's name:
# a name would leak into printed paths and can then satisfy an expect_output_not_contains.
_tree() {
  local root
  _xff_tree_seq=$((${_xff_tree_seq:-0} + 1))
  root="${BASHTEST_TMPDIR}/tree${_xff_tree_seq}"
  mkdir -p "${root}"
  : >"${root}/plain.txt"
  echo "${root}"
}

test::asking_for_archive_handling_is_a_hard_error_without_the_extra() {
  local root out rc spelling
  root="$(_tree)"
  # Every spelling that REQUESTS diving must fail the same way, naming what to rebuild
  # with - never a silent no-op that looks like "xff cannot see into this archive".
  for spelling in "--archive" "--archive=all" "--archive=roots" "-z" "-z+"; do
    out="$("$(_xff_bin)" "${spelling}" "${root}" 2>&1)" && rc=0 || rc=$?
    expect_eq "2" "${rc}"
    expect_output_contains "no archive support" "${out}"
    # The hint must name the REAL Bazel flag (`xff_archive`, not `archive`): an error telling the
    # user to rebuild with a flag that does not exist is worse than no hint at all.
    expect_output_contains "--//xff:xff_archive" "${out}"
  done
}

test::the_extras_topic_names_real_build_flags() {
  local out
  out="$("$(_xff_bin)" --help=extras 2>&1)"
  expect_output_contains "--//xff:xff_archive" "${out}"
  expect_output_contains "--//xff:xff_pcre" "${out}"
}

test::asking_for_find_behavior_needs_no_extra() {
  local root out spelling
  root="$(_tree)"
  # none / -z- ask for exactly what a lean build already does, so demanding a rebuild
  # there would be nonsense: they must walk normally.
  for spelling in "--archive=none" "-z-"; do
    out="$("$(_xff_bin)" "${spelling}" "${root}" -type f 2>&1)"
    expect_output_contains "plain.txt" "${out}"
    expect_output_not_contains "no archive support" "${out}"
  done
}

test::archive_is_off_by_default_so_a_plain_walk_is_unaffected() {
  local root out
  root="$(_tree)"
  # The xff-family default is `roots`, but a default must never trip the guard: only an
  # EXPLICIT request errors, otherwise every ordinary run would break.
  out="$("$(_xff_bin)" "${root}" -type f 2>&1)"
  expect_output_contains "plain.txt" "${out}"
  expect_output_not_contains "archive" "${out}"
}

test::help_documents_the_modes() {
  local out
  out="$("$(_xff_bin)" --help=--archive 2>&1)"
  expect_output_contains "none" "${out}"
  expect_output_contains "roots" "${out}"
  expect_output_contains "all" "${out}"
}

test_runner
