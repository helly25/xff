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
# Verifies that the extended `xff_full` binary actually LINKS every build extra that
# `--config=xff_full` enables - none silently dropped by a bad `select`, a missing `alwayslink`, or a
# backend that fails to self-register. It reads the per-extra availability straight from
# `xff --help=extras` and asserts each enabled extra reports "built into this binary".
#
# This target is `manual` (it links the extras + fetches @pcre2), so wildcard builds skip it; every
# CI test job names it explicitly and runs it under `--config=xff_full` (only the `minimal` job opts
# out of the extras). So its assertions are unconditional on purpose. A plain lean build of xff_full
# would correctly report these extras off - not this test's concern (full_binary_test covers the
# config-agnostic behaviour). As a new extra is added to `--config=xff_full`, add its line here so
# the guarantee grows with the set.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${helly25_bashtest}"

_xff_full_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/xff_full"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff_full -path '*xff/cli/xff_full' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

test::xff_full_links_the_pcre2_extra() {
  # PCRE2 (--//xff:xff_pcre, on under --config=xff_full) must be linked and reported built in - never
  # dropped. Match the bracketed status right after the name (only spaces between, no newline), so it
  # binds to pcre2's own line: a lean binary shows "[not built in ...]" there and this fails. (Bash
  # `[[ =~ ]]` lets `.` cross newlines, so a loose `pcre2.*built in` would bleed into another extra.)
  local out
  out="$("$(_xff_full_bin)" --help=extras 2>&1)"
  expect_matches "pcre2 +\[built into this binary\]" "${out}"
}

test_runner
