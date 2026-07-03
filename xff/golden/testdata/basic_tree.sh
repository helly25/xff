# SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com)
# SPDX-License-Identifier: Apache-2.0
#
# shellcheck shell=bash
#
# Fixture for the xff_golden dual-mode tests: a small, deterministic tree. Run by the
# golden driver with $PWD set to a fresh temp root. Keep byte sizes stable (goldens
# assert on them via --summary): a.txt=4, b.log=7, c.txt=2 -> total 13 across 3 files.
mkdir -p src docs
printf 'aaa\n' >src/a.txt
printf 'bbbbbb\n' >src/b.log
printf 'c\n' >docs/c.txt
