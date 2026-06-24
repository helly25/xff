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
# Binary-level test: `xff --explain` writes the effective configuration resolved
# from XFF_CONFIG, tagging each flag with provenance and omitting inactive-style
# lines. Exercises the real cli/main.cc wiring end to end (helly25/bashtest).

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${helly25_bashtest}"

# Path to the built xff binary in the test runfiles.
_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

test::explain_reflects_effective_config() {
  local cfg="${TEST_TMPDIR}/xff_config"
  printf 'common: --sort\nxff: --feature=long\nfind: --warn\n' >"${cfg}"
  local out
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --config=xff --explain)"
  local lines=()
  local line
  while IFS= read -r line; do lines+=("${line}"); done <<<"${out}"

  # common: applies under every style; xff: applies because --config=xff is active.
  expect_contains "$(printf 'user\t--sort')" "${lines[@]}"
  expect_contains "$(printf 'user\t--feature=long')" "${lines[@]}"
  # The find: line is inert (its style is not active): --warn must not surface.
  expect_not_contains "$(printf 'user\t--warn')" "${lines[@]}"
  # The CLI selector is echoed with cli provenance.
  expect_contains "$(printf 'cli\t--config=xff')" "${lines[@]}"
  # The source trace reports the active style and the consulted user config (found).
  expect_contains "# xff active style: xff" "${lines[@]}"
  expect_contains "$(printf 'source\tuser\tfound\t%s' "${cfg}")" "${lines[@]}"
}

test::config_applies_to_the_run() {
  local dir="${TEST_TMPDIR}/tree"
  mkdir -p "${dir}"
  : >"${dir}/a.txt"
  local cfg="${TEST_TMPDIR}/xff_apply_config"
  printf 'common: --format=jsonl\n' >"${cfg}"
  # With the config, the default print emits a JSONL object (line starts with '{').
  local with_cfg
  with_cfg="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" "${dir}" -name a.txt)"
  expect_eq "{" "${with_cfg:0:1}"
  # Without a config, the default print stays plain (an absolute path, not '{').
  local plain
  plain="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name a.txt)"
  expect_ne "{" "${plain:0:1}"
}

test::project_xffrc_is_policy_gated() {
  # A hostile repo .xffrc: one safe line + one sensitive -exec line. Run from
  # inside the project dir, so ./.xffrc is the (untrusted) project layer.
  local proj="${TEST_TMPDIR}/proj"
  mkdir -p "${proj}"
  printf 'common: --color=never\ncommon: -exec rm {} ;\n' >"${proj}/.xffrc"
  local xff
  xff="$(_xff_bin)"
  local out
  out="$(cd "${proj}" && XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --explain)"
  local lines=()
  local line
  while IFS= read -r line; do lines+=("${line}"); done <<<"${out}"
  # The safe line applies with project provenance; the sensitive -exec is dropped.
  expect_contains "$(printf 'project\t--color=never')" "${lines[@]}"
  expect_contains "$(printf "dropped\t'-exec' from the project .xffrc (sensitive)")" "${lines[@]}"
}

test_runner
