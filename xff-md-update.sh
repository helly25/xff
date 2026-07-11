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

# Regenerate the committed Markdown reference XFF.md from the full binary. Run this after any change
# that alters the help surface (a registry Descriptor, a global flag, help prose). //xff/cli:xff_markdown_test
# fails until XFF.md matches this output.

set -euo pipefail

cd "$(dirname "$0")"
bazel run --config=xff_full //xff/cli:xff_full -- --markdown >XFF.md
echo "Wrote XFF.md"
