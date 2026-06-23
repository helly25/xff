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
# from XFF_CONFIG, tagging each flag with its provenance and omitting lines whose
# style is not active. This is the binary-level counterpart to the loader/config
# unit tests, exercising the real cli/main.cc wiring end to end.

set -euo pipefail

xff="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/xff"
if [ ! -x "${xff}" ]; then
  xff="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/xff' 2>/dev/null | head -1)"
fi
[ -x "${xff}" ] || { echo "FAIL: xff binary not found under ${TEST_SRCDIR}" >&2; exit 1; }

cfg="${TEST_TMPDIR}/xff_config"
printf 'common: --sort\nxff: --feature=long\nfind: --warn\n' > "${cfg}"

out="$(XFF_CONFIG="${cfg}" "${xff}" --config=xff --explain)"

fail() { echo "FAIL: $1" >&2; printf 'output was:\n%s\n' "${out}" >&2; exit 1; }
assert_has() { if ! printf '%s\n' "${out}" | grep -q "$1"; then fail "missing: $1"; fi; }
assert_lacks() { if printf '%s\n' "${out}" | grep -q -- "$1"; then fail "leaked: $1"; fi; }

assert_has 'user.*--sort'          # common: line applies under every style
assert_has 'user.*--feature=long'  # xff: line applies because --config=xff is active
assert_lacks '--warn'              # find: line is inert (the find style is not active)
assert_has 'cli.*--config=xff'     # the CLI selector is echoed with cli provenance

echo "PASS: xff --explain reflects the effective config"
