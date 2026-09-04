#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

case "${1:-}" in
  --markdown) printf '# xff reference\n' ;;
  --html)
    if [[ -n "${XFF_FAKE_BAD_HTML:-}" ]]; then
      printf 'broken html\n'
    else
      printf '<!doctype html>\n<html></html>\n'
    fi
    ;;
  *) exit 2 ;;
esac
