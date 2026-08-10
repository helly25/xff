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

"""xff's cc_binary / cc_test wrappers, so the MSan suppression file is a declared runfile.

MSan is a RUNTIME sanitizer: it reads its suppression file when the instrumented process
starts (MSAN_OPTIONS=suppressions=...), not when anything is compiled. So the file has to
travel with the executable, which in bazel means `data` - it lands in the target's runfiles
and is therefore present, declared, and cache-correct, with no path outside the sandbox and
no per-target boilerplate in the BUILD files.

Every C++ binary and test in this repository loads `cc_binary` / `cc_test` from here instead
of from `@rules_cc//cc:defs.bzl`. The wrappers are transparent - same names, same attributes
- except that under `--config=msan` (which sets `--//xff:xff_msan`) they add
`//tools:msan_suppressions` to `data`. `.bazelrc` points MSAN_OPTIONS at that runfiles-
relative path.

`cc_library` is deliberately NOT wrapped: a library has no runfiles of its own and never
starts a process, so it has nothing to carry the file to.
"""

load("@rules_cc//cc:defs.bzl", _cc_binary = "cc_binary", _cc_test = "cc_test")

_SUPPRESSIONS = "//tools:msan_suppressions"

_MSAN = "//xff:xff_msan_enabled"

def _suppressions_data():
    return select({
        _MSAN: [_SUPPRESSIONS],
        "//conditions:default": [],
    })

def cc_binary(name, data = [], **kwargs):
    """`cc_binary` carrying the MSan suppression file in its runfiles under --config=msan."""
    _cc_binary(
        name = name,
        data = data + _suppressions_data(),
        **kwargs
    )

def cc_test(name, data = [], **kwargs):
    """`cc_test` carrying the MSan suppression file in its runfiles under --config=msan."""
    _cc_test(
        name = name,
        data = data + _suppressions_data(),
        **kwargs
    )
