#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com)
# SPDX-License-Identifier: Apache-2.0
#
# The single input generator for the -diff golden tests: populates two parallel trees `a` and `b`
# (b's files differ from a's), so `xff a -type f -diff=STYLE 'b/{relpath}'` walks `a`, finds each
# file, and diffs it against its `b` counterpart. -diff emits a git-style (no-timestamp) header, so
# the output is verbatim without touching mtimes.
set -euo pipefail
mkdir -p a/sub b/sub
printf 'alpha\nbeta\n' >a/one.txt
printf 'alpha\nBETA\n' >b/one.txt
printf 'gamma\ndelta\n' >a/sub/two.txt
printf 'gamma\nDELTA\n' >b/sub/two.txt
