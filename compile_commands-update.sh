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

# Generate `compile_commands.json` by extracting the compile commands in the exact
# configuration we develop in: `--config=clang-tidy` (see .bazelrc), which layers
# the hermetic LLVM/clang toolchain (`--config=clang`) and an explicit libc++ on
# top. Because the extractor's internal `aquery` runs in that same config, every
# recorded command carries the standard library, include paths and feature macros
# the `--config=clang` builds use - so clang-tidy / clangd parse exactly what the
# real build does, not whatever toolchain bazel would otherwise autodetect (which
# differs per platform: libc++ on macOS but the system libstdc++ on Linux, the mix
# that silently corrupted clang-tidy's type/member analysis).
#
# The `--config=clang-tidy` below is a RUNTIME arg to the extractor tool (it must
# follow `--`, or `bazel run` hands it to bazel); the tool forwards every non
# `--bcce-*` runtime arg to its internal aquery, which is how the config reaches it.

set -euo pipefail

function die() {
  echo "ERROR: ${*}" 1>&2
  exit 1
}

OUTPUT_BASE="$(bazel info output_base 2>/dev/null || true)"
[ -n "${OUTPUT_BASE}" ] || die "'bazel info output_base' failed; is bazel on PATH?"

# The hermetic LLVM install (headers + libs) lives in the `_llvm_llvm` repo; the
# plain `_llvm` repo only re-exports clang-format/clang-tidy/clangd.
declare -a CLANG_LOCS=(
  "${OUTPUT_BASE}/external/toolchains_llvm++llvm+llvm_toolchain_llvm_llvm/bin/clang++"
  "${OUTPUT_BASE}/external/toolchains_llvm~~llvm~llvm_toolchain_llvm_llvm/bin/clang++"
  "${OUTPUT_BASE}/external/llvm_toolchain_llvm/bin/clang++"
)

CLANG=""
function resolve_clang() {
  for LOC in "${CLANG_LOCS[@]}"; do
    if [ -x "${LOC}" ]; then
      CLANG="${LOC}"
      return
    fi
  done
}

resolve_clang
if [ -z "${CLANG}" ]; then
  # A fresh checkout (or CI runner) has not materialized the toolchain yet: it is
  # only fetched once something is actually built with `--config=clang`. The header
  # build below (also `--config=clang-tidy`) triggers that, so just build first.
  echo "Hermetic clang++ not present; fetching the toolchain via a probe build ..." 1>&2
  bazel build --config=clang-tidy //tools:show_compiler >/dev/null \
    || die "probe build '//tools:show_compiler --config=clang-tidy' failed; cannot fetch the LLVM toolchain"
  resolve_clang
fi
[ -n "${CLANG}" ] || die "Cannot find the hermetic clang++ even after a '--config=clang-tidy' build"

# Sources reachable both normally and through a build-machine tool are compiled
# twice (target + exec configuration), and both commands would be emitted. Keep
# only the target-configuration one: clang-tidy works per entry, so the exec copy
# is duplicate linting for a near-identical result. Files compiled ONLY in the
# exec configuration keep their command, so nothing leaves the compile DB.
declare -a BCCE_ARGS=("--bcce-prefer-target-config")

# Name the hermetic clang++ BINARY (not the toolchain's `cc_wrapper.sh`, which is
# what the extracted command line starts with). clang-tidy runs its own clang to
# parse, but reads this leading argument to derive the driver's target triple and
# resource directory (its built-in headers). A shell-script "compiler" leaves it
# with the wrong builtins, so the macOS SDK / libc++ headers fail to parse. All the
# OTHER flags still come from `--config=clang-tidy`; only the compiler token is
# substituted so clang-tidy can introspect a real clang.
BCCE_ARGS+=("--bcce-compiler=${CLANG}")

# The hermetic clang carries its own libc++ but no system C headers: without the
# SDK sysroot its <locale> support headers fail on `'time.h' file not found`. The
# bazel `--config=clang` toolchain supplies this itself, but the extracted commands
# do not carry it, so make it explicit here. Linux needs no such flag.
if [ "$(uname -s)" = "Darwin" ]; then
  SDKROOT_PATH="$(xcrun --show-sdk-path 2>/dev/null || true)"
  [ -n "${SDKROOT_PATH}" ] || die "'xcrun --show-sdk-path' failed; install the Xcode command line tools"
  BCCE_ARGS+=("--bcce-copt=-isysroot${SDKROOT_PATH}")
fi

# Materialize the generated / virtual-include headers the recorded commands will
# reference (e.g. xff/license/notice.h, xff/regex/backend.h from the local
# @xff_extras_api module, served through bazel-out `_virtual_includes` symlink
# forests that exist only once their cc_library is built). The aquery records those
# include paths but does not build them, so a fresh checkout / CI runner has the
# paths but not the files and clang-tidy aborts with `'xff/.../foo.h' file not
# found`. Build them in the SAME config the aquery runs in (`--config=clang-tidy`),
# so the forests land exactly where the DB points; a fresh checkout / CI runner also
# fetches the hermetic toolchain here.
echo "Building generated / virtual-include headers in --config=clang-tidy so the compile DB resolves ..." 1>&2
bazel build --config=clang-tidy //... >/dev/null \
  || die "'bazel build --config=clang-tidy //...' failed; cannot materialize the headers the compile DB references"

bazel run @bazel_compile_commands_extractor//:refresh_all -- --config=clang-tidy "${BCCE_ARGS[@]}"

# macOS only: the `--config=clang` toolchain (toolchains_llvm) points libc++ at the Xcode SDK by
# emitting `-nostdinc++ -cxx-isystem <SDK>/usr/include/c++/v1`, while `-resource-dir` is the
# hermetic clang's. clang-tidy parses with the hermetic clang++ (see --bcce-compiler), so that SDK
# libc++ against a hermetic resource dir is a mismatch that silently degrades its analysis (spurious
# unused-variable / const-correctness findings). The hermetic clang++ finds its OWN libc++ when left
# to its default search, so drop just those two flags from the DB; `-isysroot` stays for the system
# C headers. Linux never emits the SDK `-cxx-isystem`, so this is a no-op there and stays gated.
if [ "$(uname -s)" = "Darwin" ]; then
  echo "macOS: dropping the SDK libc++ -cxx-isystem so the hermetic clang++ uses its own libc++ ..." 1>&2
  python3 - compile_commands.json <<'PY'
import json
import sys

path = sys.argv[1]
with open(path, encoding="utf-8") as f:
    entries = json.load(f)


def strip(args):
    out = []
    skip = False
    for i, arg in enumerate(args):
        if skip:
            skip = False
            continue
        if arg == "-nostdinc++":
            continue
        nxt = args[i + 1] if i + 1 < len(args) else ""
        if arg == "-cxx-isystem" and nxt.endswith("/c++/v1") and "MacOSX.sdk" in nxt:
            skip = True  # also drop the path argument that follows
            continue
        out.append(arg)
    return out


for entry in entries:
    entry["arguments"] = strip(entry["arguments"])
with open(path, "w", encoding="utf-8") as f:
    json.dump(entries, f, indent=2)
    f.write("\n")
PY
fi
echo "OK"
