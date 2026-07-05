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
  # common: always applies; a named config (myx:) applies only when --config=myx is active; an
  # inactive named config (other:) stays inert. (Bare preset selectors like `xff:` are rejected -
  # see explain_rejects_preset_overloading_config.)
  printf 'common: --sort\nmyx: --feature=long\nother: --warn\n' >"${cfg}"
  local out
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --config=myx --explain)"
  local lines=()
  local line
  while IFS= read -r line; do lines+=("${line}"); done <<<"${out}"

  # common: applies always; myx: applies because --config=myx is active.
  expect_contains "$(printf 'user\t--sort')" "${lines[@]}"
  expect_contains "$(printf 'user\t--feature=long')" "${lines[@]}"
  # The other: line is inert (its named config is not active): --warn must not surface.
  expect_not_contains "$(printf 'user\t--warn')" "${lines[@]}"
  # The CLI selector is echoed with cli provenance.
  expect_contains "$(printf 'cli\t--config=myx')" "${lines[@]}"
  # The source trace reports the consulted user config (found).
  expect_contains "$(printf 'source\tuser\tfound\t%s' "${cfg}")" "${lines[@]}"
}

test::explain_rejects_preset_overloading_config() {
  # A config file may not attach behavior to a built-in preset: a bare `xff:` / `find:` selector
  # (no named config) is dropped in every layer, so a plain preset run stays reproducible.
  local cfg="${TEST_TMPDIR}/xff_overload_config"
  printf 'xff: --feature=long\ncommon: --sort\n' >"${cfg}"
  local dir="${TEST_TMPDIR}/ov"
  mkdir -p "${dir}"
  : >"${dir}/a.txt"
  local out
  # A normal run warns on stderr that the bare-preset line was dropped.
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --config=xff "${dir}" -name a.txt 2>&1)"
  expect_matches 'cannot change a preset' "${out}"
  # --explain records the drop in its trace, does not resolve the bare-preset flag, keeps common:.
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --config=xff --explain 2>&1)"
  expect_matches "dropped[[:space:]]'xff:' in the" "${out}"
  expect_not_matches 'user[[:space:]]--feature=long' "${out}"
  expect_matches 'user[[:space:]]--sort' "${out}"
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
  # --project-config=on opts in to the per-directory file (off by default); the gate still runs.
  out="$(cd "${proj}" && XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --project-config=on --explain)"
  local lines=()
  local line
  while IFS= read -r line; do lines+=("${line}"); done <<<"${out}"
  # The safe line applies with project provenance; the sensitive -exec is dropped.
  expect_contains "$(printf 'project\t--color=never')" "${lines[@]}"
  expect_contains "$(printf "dropped\t'-exec' from the project .xffrc (sensitive)")" "${lines[@]}"
}

test::project_cascade_applies_ancestor_xffrc() {
  # A .xffrc at a PARENT dir applies to a search root in a subdir (gitignore-style
  # ancestor cascade), surfacing in --explain with project provenance.
  local base="${TEST_TMPDIR}/cascade"
  mkdir -p "${base}/sub"
  printf 'common: --color=never\n' >"${base}/.xffrc" # ancestor (parent) project file
  local out
  # Opt in with --project-config=on; the ancestor cascade is a project-layer source.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --project-config=on --explain "${base}/sub")"
  local lines=()
  local line
  while IFS= read -r line; do lines+=("${line}"); done <<<"${out}"
  # The ancestor .xffrc is discovered as a project-layer source and contributes.
  expect_contains "$(printf 'project\t--color=never')" "${lines[@]}"
}

test::project_config_is_off_by_default_and_warns() {
  # A per-directory .xffrc lives in a tree we may not control, so it is off unless opted in.
  local proj="${TEST_TMPDIR}/pcmode"
  mkdir -p "${proj}"
  : >"${proj}/a.txt"
  printf 'common: --format=jsonl\n' >"${proj}/.xffrc" # a project file that WOULD change output
  local xff
  xff="$(_xff_bin)"
  # Default (warn): the project file is NOT applied (output stays plain, no JSONL brace) and a
  # stderr note fires because a project .xffrc was found.
  local out
  out="$(cd "${proj}" && XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" . -name a.txt 2>&1)"
  expect_matches 'per-directory .xffrc was found but ignored' "${out}"
  expect_not_matches '\{' "${out}" # --format=jsonl not applied -> no object brace
  # off: ignored silently (no note).
  out="$(cd "${proj}" && XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --project-config=off . -name a.txt 2>&1)"
  expect_not_matches 'found but ignored' "${out}"
  expect_not_matches '\{' "${out}"
  # on: applied -> the JSONL object appears.
  out="$(cd "${proj}" && XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --project-config=on . -name a.txt 2>&1)"
  expect_matches '\{' "${out}"
}

test_runner
