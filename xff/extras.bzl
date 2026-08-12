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

# THE list of composable extras: one entry per removable module under `extra_modules/`. Everything
# else about extras is derived from it - the `--//xff:xff_<flag>` build flags, their config_settings,
# the `:full_build` OR over them, and `//xff:all_extras_cc` (what `xff_full` links) are all generated
# by looping over this list in xff/BUILD.bazel. So adding an extra is: its `bazel_dep` +
# `local_path_override` in MODULE.bazel, one entry here, and one `--config=xff_docs` line in .bazelrc.
#
# `tools/extras.py --check-configured` (a pre-commit hook) asserts that this list and .bazelrc between
# them cover every extra MODULE.bazel declares, so "the full documented surface" cannot quietly become
# "the extras someone remembered". The flag name is NOT derivable from the module name - `xff_pcre2`
# is gated by `--//xff:xff_pcre` - which is exactly why the pairing is written down once, here.
XFF_EXTRAS = [
    struct(
        module = "xff_archive",
        flag = "xff_archive",
        target = "@xff_archive//:archive_register_cc",
        summary = "archive diving (libarchive + the phar reader)",
    ),
    struct(
        module = "xff_pcre2",
        flag = "xff_pcre",
        target = "@xff_pcre2//:pcre2_backend_cc",
        summary = "the PCRE2 regex backend",
    ),
]

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

# `target_compatible_with` for a target that needs the ARCHIVE extra specifically (rather than any
# full build): the end-to-end tests that dive into a container only mean anything in a binary that
# links a reader, so `--config=xff_full` (PCRE2 only) must skip them and `--config=xff_docs` runs
# them. Keyed on the extra's own flag, which is the "is archive on" question - a different one from
# XFF_FULL_ONLY's "is this the full build".
XFF_ARCHIVE_ONLY = select({
    "//xff:xff_archive_enabled": [],
    "//conditions:default": ["@platforms//:incompatible"],
})

# The `deps` for //xff:all_extras_cc: one `select` per extra, summed. A BUILD file cannot write this
# as a comprehension (a list OF selects is not a select), and summing them is exactly what "link the
# extras that are on" means - so it lives here, next to the list it walks.
def all_extras_deps():
    deps = []
    for extra in XFF_EXTRAS:
        deps = deps + select({
            ":" + extra.flag + "_enabled": [extra.target],
            "//conditions:default": [],
        })
    return deps
