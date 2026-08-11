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

"""xff's cc_* rules: one load site, and a queryable sanitizer-ready set.

Every C++ target in this module declares itself through these macros rather than through
`@rules_cc//cc:defs.bzl` directly, and the `no-raw-rules-cc-load` pre-commit hook enforces that, so
what the wrappers provide is a property of the whole module rather than of whether somebody
remembered.

What they do:

* `cc_binary` / `cc_test` tag every target `msan`, so the set is queryable rather than implied:
  `bazel query 'attr(tags, msan, kind(cc_test, //...))'`. `tags` takes no `select()`, so the tag is
  unconditional and means "this target routes through the wrapper".
* `cc_library` is a plain PASSTHROUGH. It exists so that every BUILD file has ONE load site for cc
  rules, which is what lets the hook forbid the raw ones outright.

What they deliberately do NOT do, and why each was tried and dropped:

* They do not carry a RUNTIME suppression file. They used to, gated on `--//xff:xff_msan`, because
  MSan is a runtime sanitizer and a suppression file has to travel with the executable. The premise
  was wrong: MSan has NO runtime suppression support. `suppressions=` is a sanitizer_common flag that
  MSan parses and never consumes - compiler-rt's msan sources contain no suppression machinery at all,
  and only the tools that build a SuppressionContext (ASan's interceptors, LSan, TSan, UBSan) act on
  it. The file was therefore never read once, which is also why tests in the extras modules passed
  under MSan while having no such file in their runfiles. The real mechanism for a future false
  positive is the COMPILE-time `-fsanitize-ignorelist` (see `.bazelrc`).
* They do not swap in the MSan-instrumented C++ standard library. That is the TOOLCHAIN's job, via
  toolchains_llvm's `--features=msan` and the `libcxx_url` overlay (see `.bazelrc` and
  `bazelmod/llvm.MODULE.bazel`). An earlier version added the instrumented libc++ to every target's
  `deps`, which cannot work: a dep's include path is additive, so the toolchain's own libc++ stays on
  the search path alongside it.
"""

load("@rules_cc//cc:defs.bzl", _cc_binary = "cc_binary", _cc_library = "cc_library", _cc_test = "cc_test")

# Marks a target as routing through these wrappers, so the property is queryable rather than
# implied. Unconditional because `tags` cannot take a `select()`.
_MSAN_TAG = "msan"

def cc_binary(name, tags = [], **kwargs):
    """`cc_binary`, tagged `msan` so the sanitizer-ready set is queryable."""
    _cc_binary(
        name = name,
        tags = tags + [_MSAN_TAG],
        **kwargs
    )

def cc_test(name, tags = [], **kwargs):
    """`cc_test`, tagged `msan` so the sanitizer-ready set is queryable."""
    _cc_test(
        name = name,
        tags = tags + [_MSAN_TAG],
        **kwargs
    )

def cc_library(name, **kwargs):
    """`cc_library`: a passthrough, so every BUILD file has ONE load site for cc rules."""
    _cc_library(
        name = name,
        **kwargs
    )
