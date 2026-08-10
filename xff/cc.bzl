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

"""xff's cc_* rules: one load site, and MSan-ready executables.

Every C++ target in this module declares itself through these macros rather than through
`@rules_cc//cc:defs.bzl` directly, and the `no-raw-rules-cc-load` pre-commit hook enforces
that - so "is this target MSan-ready" is a property of the whole module, not of whether
somebody remembered.

What the wrappers do:

* `cc_binary` / `cc_test` add `//tools:msan_suppressions` to `data` under `--config=msan`
  (gated by `--//xff:xff_msan`). MSan is a RUNTIME sanitizer: it reads its suppression file
  when the instrumented process starts, so the file has to travel with the executable, and
  runfiles is exactly that - declared, cache-correct, inside the sandbox, and named by a
  relative path from the runfiles working directory (see `MSAN_OPTIONS` in `.bazelrc`).
  A bare `--copt` could not do this: it is not an input, `%workspace%` is not expanded
  inside a copt value, and an absolute include-ish path is rejected as outside the
  execution root.
* they also tag the target `msan`, so the property is queryable rather than implied:
  `bazel query 'attr(tags, msan, kind(cc_test, //...))'` lists exactly the tests that carry
  the suppression file. `tags` is not configurable (it takes no `select()`), so the tag is
  unconditional and means "this target routes through the wrapper", which is what makes it
  useful as a cross-check.
* `cc_library` is a plain PASSTHROUGH. A library has no runfiles and starts no process, so
  there is nothing to give it - it exists so that every BUILD file has ONE load site for
  cc rules, which is what lets the hook forbid the raw ones outright.
"""

load("@rules_cc//cc:defs.bzl", _cc_binary = "cc_binary", _cc_library = "cc_library", _cc_test = "cc_test")

_SUPPRESSIONS = "//tools:msan_suppressions"

# The MSan-instrumented libc++ (tools/build_msan_libcxx.sh -> .msan-libcxx, exposed as a
# repository). Depending on it is how the instrumented standard library actually gets used:
# its `includes` emits an execroot-relative -isystem in the ORDINARY include chain, which is
# what libc++'s C-header shims need (they reach glibc via #include_next), and its `srcs` are
# the instrumented archives. Naming the directory in copts/linkopts instead failed three
# different ways - unexpanded %workspace%, "outside of the execution root", and broken
# #include_next - all recorded in TODO.md.
_LIBCXX = "@msan_libcxx//:libcxx"

_MSAN = "//xff:xff_msan_enabled"

# Marks a target as routing through these wrappers, hence carrying the MSan suppression
# file. Unconditional because `tags` cannot take a `select()`.
_MSAN_TAG = "msan"

def _suppressions_data():
    return select({
        _MSAN: [_SUPPRESSIONS],
        "//conditions:default": [],
    })

def _libcxx_deps():
    return select({
        _MSAN: [_LIBCXX],
        "//conditions:default": [],
    })

def cc_binary(name, data = [], tags = [], deps = [], **kwargs):
    """`cc_binary`: MSan suppression file in runfiles, instrumented libc++ in deps."""
    _cc_binary(
        name = name,
        data = data + _suppressions_data(),
        deps = deps + _libcxx_deps(),
        tags = tags + [_MSAN_TAG],
        **kwargs
    )

def cc_test(name, data = [], tags = [], deps = [], **kwargs):
    """`cc_test`: MSan suppression file in runfiles, instrumented libc++ in deps."""
    _cc_test(
        name = name,
        data = data + _suppressions_data(),
        deps = deps + _libcxx_deps(),
        tags = tags + [_MSAN_TAG],
        **kwargs
    )

def cc_library(name, deps = [], **kwargs):
    """`cc_library` compiled against the instrumented libc++ under --config=msan.

    A library has no runfiles and starts no process, so it gets no suppression file - but its
    translation units are where most of our code is compiled, so it does need the instrumented
    standard library's headers, which arrive through this dep.
    """
    _cc_library(
        name = name,
        deps = deps + _libcxx_deps(),
        **kwargs
    )
