#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com)
# SPDX-License-Identifier: Apache-2.0
#
# The single input generator for the -diff golden tests: populates two parallel trees `a` and
# `b` (b's files differ from a's) with fixed mtimes, so `xff a -type f -diff=STYLE 'b/{relpath}'`
# walks `a`, finds each file, and diffs it against its `b` counterpart with a verbatim header.
set -euo pipefail
mkdir -p a/sub b/sub
printf 'alpha\nbeta\n' >a/one.txt
printf 'alpha\nBETA\n' >b/one.txt
printf 'gamma\ndelta\n' >a/sub/two.txt
printf 'gamma\nDELTA\n' >b/sub/two.txt
find a b -type f -exec touch -t 200001010000 {} +
