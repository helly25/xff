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
# The //xff alias is config-adaptive: it resolves to the lean //xff/cli:xff by default and to the
# extended //xff/cli:xff_full in a full build. This test asserts the FULL direction. It is
# XFF_FULL_ONLY, so it only builds under --config=xff_full, where //xff MUST resolve to the full
# binary (PCRE2 compiled in) while //xff/cli:xff stays lean. Both binaries come in via $(rootpath
# ...) args, so the paths reflect the alias's actual resolution (break the select and $1 becomes the
# lean binary, failing the built-in assertion). The binaries run straight from runfiles -- no `bazel
# run` -- so the captured output is only the command's own, with no bazel startup noise.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${helly25_bashtest}"

# $1 / $2 are $(rootpath ...) values (workspace-relative); resolve them to real runfiles paths.
alias_bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/${1:?missing //xff rootpath arg}"
lean_bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/${2:?missing //xff/cli:xff rootpath arg}"

test::alias_resolves_to_full_while_lean_target_stays_lean() {
  # --help=extras marks each extra "[built into this binary]" or "[not built in; ...]".
  local alias_out lean_out
  alias_out="$("${alias_bin}" --help=extras 2>&1)"
  lean_out="$("${lean_bin}" --help=extras 2>&1)"
  # //xff (the alias) is the full binary in this build: PCRE2 is compiled in.
  expect_output_contains 'pcre2' "${alias_out}"
  expect_output_contains 'built into this binary' "${alias_out}"
  # //xff/cli:xff is always lean, even under --config=xff_full: nothing is compiled in.
  expect_output_contains 'pcre2' "${lean_out}"
  expect_output_not_contains 'built into this binary' "${lean_out}"
}

test_runner
