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
# End-to-end test of the extended `xff_full` binary (#85 / composable extras, #115). This asserts
# the dual-binary scaffolding: `xff_full` is a real, runnable binary whose `_full` invocation name
# maps back to the xff style (so it behaves exactly as `xff`), and - because this build did not link
# the PCRE2 extra - `--regextype=PCRE2` is still a usage error, never a silent RE2 fallback. The
# `--config=xff_full` build that links the backend (so PCRE2 works) is exercised by #85's own tests.

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

test::full_binary_resolves_to_the_xff_style_via_argv0() {
  # `-grep` is an xff extension the find style rejects; that `xff_full` accepts it proves its
  # `_full` invocation name resolved to the xff style (DefaultStyleForProgram strips `_full`).
  local root out rc
  root="$(mktemp -d)"
  printf 'has TODO here\n' >"${root}/f.txt"
  out="$("$(_xff_full_bin)" "${root}" -type f -grep 'TODO' 2>&1)" && rc=0 || rc=$?
  rm -rf "${root}"
  expect_eq "0" "${rc}"
  expect_matches '/f\.txt:1:has TODO here' "${out}"
}

test::full_binary_without_the_pcre_extra_rejects_pcre2() {
  # This build did not link the PCRE2 backend (the extra is off by default), so even the full binary
  # rejects `--regextype=PCRE2` with a usage error (exit 2). A `--config=xff_full` build accepts it.
  local root out rc
  root="$(mktemp -d)"
  printf 'x\n' >"${root}/f.txt"
  out="$("$(_xff_full_bin)" --regextype=PCRE2 "${root}" -grep 'x' 2>&1)" && rc=0 || rc=$?
  rm -rf "${root}"
  expect_eq "2" "${rc}"
  expect_matches 'not built into this binary' "${out}"
}

test_runner
