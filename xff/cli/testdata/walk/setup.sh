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
# Shared input generator for the walk/matching golden tests (golden.bzl). Populates a small,
# deterministic tree under `root/` whose output shape is stable across platforms: the goldens
# assert only relative paths, basenames, type chars and depths -- no mtime / size / inode / owner,
# so `--sort=tree` makes every case reproducible on macOS and Linux alike.
set -euo pipefail
mkdir -p root/src root/docs
printf 'title\n' >root/README.md
printf 'all:\n' >root/Makefile
printf 'int main() { return 0; }\n' >root/src/main.cc
printf 'int util();\n' >root/src/util.cc
printf 'int util();\n' >root/src/util.h
printf 'docs\n' >root/docs/guide.md
