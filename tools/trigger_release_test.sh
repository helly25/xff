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
# Unit test for the pure, verifiable helpers in tools/trigger_release.sh - the
# next-version choice (default minor bump, explicit override) and the strictly
# greater X.Y.Z check that keeps it appropriate. The script is sourced behind its
# main-guard, so no git/tag/PR side effects run. Run directly or via pre-commit.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
source "${HERE}/trigger_release.sh"
set +e # collect all failures rather than aborting on the first

FAILED=0
fail() {
  echo "FAIL: ${*}" >&2
  FAILED=1
}

# next_release_version(current, explicit) must equal want.
expect_next() {
  got="$(next_release_version "${1}" "${2}" 2>/dev/null)"
  [[ "${got}" == "${3}" ]] || fail "next_release_version(${1}, '${2}') = '${got}', want '${3}'"
}

# next_release_version(current, explicit) must fail (die).
expect_next_dies() {
  if out="$(next_release_version "${1}" "${2}" 2>&1)"; then
    fail "next_release_version(${1}, '${2}') should have failed; got '${out}'"
  fi
}

# Default is a minor bump (patch reset to 0), appropriate for a pre-1.0 series.
expect_next 0.1.0 "" 0.2.0
expect_next 0.2.3 "" 0.3.0
expect_next 1.9.0 "" 1.10.0

# An explicit next version overrides the default (patch or major).
expect_next 0.1.0 0.1.1 0.1.1
expect_next 0.1.0 1.0.0 1.0.0

# The next version must be a valid X.Y.Z strictly greater than the release.
expect_next_dies 0.2.0 0.1.0 # lower
expect_next_dies 0.2.0 0.2.0 # equal
expect_next_dies 0.1.0 abc   # not a version
expect_next_dies 0.1.0 0.1   # not X.Y.Z

# version_gt component-wise numeric comparison.
version_gt 0.2.0 0.1.0 || fail "version_gt 0.2.0 0.1.0 should be true"
version_gt 1.0.0 0.9.9 || fail "version_gt 1.0.0 0.9.9 should be true"
version_gt 0.10.0 0.9.0 || fail "version_gt 0.10.0 0.9.0 should be true (numeric, not lexical)"
version_gt 0.1.0 0.2.0 && fail "version_gt 0.1.0 0.2.0 should be false"
version_gt 0.1.0 0.1.0 && fail "version_gt on equal should be false"

# max_version: the greatest of the arguments, empty for none, numeric compare.
[[ "$(max_version)" == "" ]] || fail "max_version() should be empty"
[[ "$(max_version 0.1.0)" == "0.1.0" ]] || fail "max_version single arg"
[[ "$(max_version 0.1.0 0.2.0 0.1.5)" == "0.2.0" ]] || fail "max_version should pick the highest"
[[ "$(max_version 0.9.0 0.10.0)" == "0.10.0" ]] || fail "max_version should be numeric, not lexical"

# release_versions: strip the v and keep only X.Y.Z release tags (drop the rest).
got="$(printf 'v0.1.0\nv0.2.0\nmain\nv1.2\nv0.10.0\nnightly\n' | release_versions | tr '\n' ' ')"
[[ "${got}" == "0.1.0 0.2.0 0.10.0 " ]] || fail "release_versions filter/strip: got '${got}'"

if [[ "${FAILED}" -ne 0 ]]; then
  echo "trigger_release_test: FAILED" >&2
  exit 1
fi
echo "trigger_release_test: all tests passed"
