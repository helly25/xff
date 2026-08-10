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

# The BUILD file for @msan_libcxx: the MSan-instrumented libc++ that
# tools/build_msan_libcxx.sh installs into .msan-libcxx/ (Linux only, --config=msan only).
#
# Wrapping it as a cc_library, rather than naming the directory in copts/linkopts, is what
# makes the swap work at all:
#
# * `includes = [...]` has bazel emit an EXECROOT-RELATIVE -isystem, so it passes the
#   "include path outside of the execution root" validation that rejects an absolute one -
#   and it lands in the ORDINARY include chain. That matters because libc++ ships
#   `ctype.h` / `string.h` / `cwchar` shims that reach glibc's copies via `#include_next`;
#   from a `-cxx-isystem` (C++-only) chain that never finds /usr/include, which is why the
#   previous attempt failed with "unresolved using declaration" on ::memcpy and
#   "use of undeclared identifier '_ISalpha'".
# * `srcs` makes the instrumented archives real link inputs, replacing -L / -rpath / -lc++.
#
# Everything here is a normal declared dependency, so bazel stages it, tracks it, and
# rebuilds when it changes.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "libcxx",
    # Wildcards on purpose (buildifier rejects a constant glob pattern), and allow_empty
    # because which archives the runtimes build produces depends on its cmake options.
    srcs = glob(
        [
            "lib/libc++*.a",
            "lib/libunwind*.a",
        ],
        allow_empty = True,
    ),
    hdrs = glob(
        ["include/c++/v1/**"],
        allow_empty = False,
    ),
    includes = ["include/c++/v1"],
    visibility = ["//visibility:public"],
)
