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
# Guard (pre-commit): a bashtest must assert on captured output with helly25/bashtest's
# content matchers (expect_output_contains / expect_output_not_contains for a literal
# substring; expect_matches / expect_not_matches for an ERE), never a hand-rolled shell
# `grep`. A piped `printf ... | grep -q` misfires on SIGPIPE under `set -o pipefail`, and
# an `$(grep -q ... && echo yes)` throws away the output on failure; the matchers do
# neither and print the offending text. See STYLE_CPP.md ("Shell / binary-level tests").
#
# It flags `grep` only at a command position (line start or right after | ; & ( ! or a
# && / ||), so the xff `-grep` primary under test and the word "grep" in prose are not
# flagged; comments are stripped before scanning.
set -euo pipefail

# grep as a command: start-of-line or after a pipeline/command separator (the last char
# of && / || is already covered by the & / | in the class), then optional space, then the
# whole word `grep`. `-grep` (the flag) is preceded by `-`, so it never matches.
readonly PATTERN='(^|[|;&(!`])[[:space:]]*grep([[:space:]]|$)'

status=0
for file in "$@"; do
  # Strip comments (# to end of line) so prose like "grep's anchoring" or "grep -o" in a
  # comment is not flagged; a stripped `#` inside code can only cause a miss, never a
  # false positive, which is acceptable for a guard.
  hits="$(sed 's/#.*//' "${file}" | grep -nE "${PATTERN}" || true)"
  if [[ -n "${hits}" ]]; then
    while IFS= read -r line; do
      echo "${file}:${line}"
    done <<<"${hits}"
    status=1
  fi
done

if [[ "${status}" -ne 0 ]]; then
  echo
  echo "Hand-rolled shell 'grep' found in a bashtest (above)."
  echo "Assert on captured output with bashtest content matchers instead:"
  echo "  expect_output_contains / expect_output_not_contains  (literal substring)"
  echo "  expect_matches / expect_not_matches                  (ERE, whole-output)"
  echo "See STYLE_CPP.md, section 'Shell / binary-level tests'."
fi
exit "${status}"
