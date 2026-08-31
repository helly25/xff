#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

# Aquery removes `manual` targets while expanding wildcards, so the compile database must name every
# manual target tagged `clang-tidy` explicitly. This check makes the tag the intent and the list an
# enforced build detail rather than two sources of truth that can drift.

set -euo pipefail

if ! command -v bazel >/dev/null 2>&1; then
  echo "clang-tidy-targets: skipped (no bazel on PATH)." 1>&2
  exit 0
fi

readonly TARGETS_BZL="bazelmod/clang_tidy_targets.bzl"

if ! TAGGED="$(bazel query 'attr(tags, "clang-tidy", //...)' 2>/dev/null | sed 's|^@@||' | sort)" \
  || [ -z "${TAGGED}" ]; then
  echo "clang-tidy-targets: skipped (bazel query returned nothing; cannot verify)." 1>&2
  exit 0
fi

LISTED="$(sed -n 's|^ *"\(//[^"]*\)",$|\1|p' "${TARGETS_BZL}" | sort)"
if [ "${TAGGED}" = "${LISTED}" ]; then
  exit 0
fi

echo "ERROR: ${TARGETS_BZL} and the 'clang-tidy' tag disagree." 1>&2
echo "  Tagged but not listed (missing from compile_commands.json):" 1>&2
comm -23 <(printf '%s\n' "${TAGGED}") <(printf '%s\n' "${LISTED}") | sed 's/^/    /' 1>&2
echo "  Listed but not tagged (stale entries):" 1>&2
comm -13 <(printf '%s\n' "${TAGGED}") <(printf '%s\n' "${LISTED}") | sed 's/^/    /' 1>&2
exit 1
