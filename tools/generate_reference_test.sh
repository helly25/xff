#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

generator="${TEST_SRCDIR}/${TEST_WORKSPACE}/tools/generate_reference.sh"
fixture="$(test_tmpdir reference-generator)"
fake="${fixture}/xff"
markdown="${fixture}/XFF.md"
html="${fixture}/XFF.html"

cp "${TEST_SRCDIR}/${TEST_WORKSPACE}/tools/generate_reference_fake.sh" "${fake}"
chmod +x "${fake}"

test::generates_both_formats_and_preserves_them_on_usage_error() {
  local output rc
  output="$("${generator}" "${fake}" "${markdown}" "${html}" 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_eq "# xff reference" "$(<"${markdown}")"
  expect_eq $'<!doctype html>\n<html></html>' "$(<"${html}")"

  output="$("${generator}" "${fake}" "${markdown}" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "usage:" "${output}"
  expect_eq "# xff reference" "$(<"${markdown}")"
  expect_eq $'<!doctype html>\n<html></html>' "$(<"${html}")"

  output="$(XFF_FAKE_BAD_HTML=1 "${generator}" "${fake}" "${markdown}" "${html}" 2>&1)" && rc=0 || rc=$?
  expect_eq "1" "${rc}"
  expect_output_contains "not a standalone document" "${output}"
  expect_eq "# xff reference" "$(<"${markdown}")"
  expect_eq $'<!doctype html>\n<html></html>' "$(<"${html}")"
}
