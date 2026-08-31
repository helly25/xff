#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
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
# Guard (pre-commit): a CHECK over a COMPARISON uses the comparing macro - CHECK_NE(p, nullptr),
# CHECK_EQ(size, want) - never CHECK(p != nullptr) / CHECK(size == want). The comparing form prints
# BOTH operands when it fires; the boolean form prints only the source text of the expression, so a
# production crash says "CHECK failed: size == want" and nothing about what size actually was.
# Same rule for DCHECK / QCHECK and their absl ABSL_-prefixed spellings. See STYLE_CPP.md.
set -euo pipefail

# A comparison operator inside the parentheses of a boolean CHECK. `<` / `>` are deliberately NOT
# matched: they appear in template arguments (`CHECK(std::holds_alternative<T>(v))`) far more often
# than as comparisons, and `<=` / `>=` cover the ordering cases that matter.
readonly PATTERN='\b(ABSL_)?(D|Q)?CHECK\([^;]*(!=|==|<=|>=)'

check_status=0
for file in "$@"; do
  # Strip comments so prose naming the anti-pattern is not flagged.
  hits="$(sed 's|//.*||' "${file}" | grep -nE "${PATTERN}" || true)"
  if [[ -n "${hits}" ]]; then
    echo "${file}: use the comparing macro (CHECK_NE / CHECK_EQ / CHECK_LE / CHECK_GE), which prints"
    echo "  both operands, instead of a comparison inside a boolean CHECK - see STYLE_CPP.md:"
    while IFS= read -r hit; do
      echo "  ${file}:${hit}"
    done <<<"${hits}"
    check_status=1
  fi
done
exit "${check_status}"
