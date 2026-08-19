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
# Guard (pre-commit): bashtest owns scratch allocation through `test_tmpdir`. A local mktemp,
# BASHTEST_TMPDIR child path, sequence counter, caller-variable result, or TEST_TMPDIR fixture path
# bypasses its uniqueness and retention policy. See STYLE_SH.md ("Tests and temporary files").
set -euo pipefail

readonly PATTERN='mktemp|BASHTEST_TMPDIR}/|_xff_tree_seq|printf[[:space:]]+-v|(^|[[:space:]])(dir|root|tree|tmp|proj|fix|home|FIX|GITFIX)="?[$][{]TEST_TMPDIR[}]/'

status=0
for file in "$@"; do
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
  echo "Hand-rolled temporary-directory allocation found in a bashtest (above)."
  echo 'Allocate with: dir="$(test_tmpdir name)"'
  echo "Return helper results on stdout; do not mutate caller variables or global counters."
  echo "See STYLE_SH.md, section 'Tests and temporary files'."
fi
exit "${status}"
