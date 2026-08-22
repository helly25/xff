#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

readonly version="2.5"
readonly sha256="7e5e5a154bd5f3557659c328cab376764e7abd238bb403c424472c296b175126"
readonly destination="${1:?usage: install_lcov.sh DESTINATION}"

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
archive="${work}/lcov.tar.gz"

curl --fail --location --retry 3 \
  "https://github.com/linux-test-project/lcov/releases/download/v${version}/lcov-${version}.tar.gz" \
  --output "${archive}"
printf '%s  %s\n' "${sha256}" "${archive}" | sha256sum --check --status
tar -xzf "${archive}" --directory "${work}"
make --directory "${work}/lcov-${version}" install PREFIX="${destination}"
