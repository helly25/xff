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
# "Is this the full build" lives in ONE place: the `//xff:full_build` config_setting_group (see
# xff/BUILD.bazel), an OR over the enabled extras (today just PCRE2, which `--config=xff_full` sets;
# add archive's setting there when #83 lands). Both this gate and the `//xff` alias key on it, so a
# new extra never needs a matching edit here.
XFF_FULL_ONLY = select({
    "//xff:full_build": [],
    "//conditions:default": ["@platforms//:incompatible"],
})
