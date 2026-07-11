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

"""Shared build-config helpers for xff's composable extras.

Loaded (`load("//xff:extras.bzl", ...)`) by any package that gates a target on the extras / full
build, so the condition lives in one place instead of being copied per BUILD file.
"""

# A `target_compatible_with` value that makes a target exist ONLY in the extras / full build
# (`--config=xff_full`). In a lean build the target is incompatible, so `bazel test //...` skips it
# (Bazel's incompatible-target skipping) rather than building it; in full mode it is included with no
# `manual` tag and no explicit naming in the build command. (`tags` cannot express this - it is a
# non-configurable attribute, so it takes no `select()`; `target_compatible_with` is the right seam.)
#
# The full build is identified today by the PCRE2 flag (`--//xff:xff_pcre`, which `--config=xff_full`
# sets). When a second extra is wired in (archive, #83), turn this into an OR over the per-extra
# config_settings via `@bazel_skylib//lib:selects` `config_setting_group`.
XFF_FULL_ONLY = select({
    "//xff:xff_pcre_enabled": [],
    "//conditions:default": ["@platforms//:incompatible"],
})
