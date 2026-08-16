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
# Run the FUSE tests on LINUX, from a mac, before pushing.
#
# Why this exists: macOS has no fuse3, so every mounting test SKIPS here - `bazel test` goes green
# without executing a line of the kernel path. That is not a theoretical gap: --archive-mount (#183)
# shipped a use-after-free and an aborted-mount bug that only Linux CI could see, and each round trip
# cost minutes. This runs the same tests in a container with /dev/fuse, in seconds.
#
# Needs a Linux VM with docker (colima works: `colima start`). The bazel cache lives in a named
# volume, so the first run builds and later runs are fast.
#
#   tools/fuse_linux_test.sh          # functional: the fuse tests + the CLI mount test, sandboxed
#   tools/fuse_linux_test.sh tsan     # the CLI mount test under ThreadSanitizer
#
# TSan note: this harness is aarch64 on Apple silicon, where TSan needs reduced ASLR entropy or it
# aborts with "unexpected memory mapping" before running anything - hence --privileged and the
# sysctl. The repo's own --config=tsan is not used because it pulls the hermetic x86_64 clang
# toolchain, which has no aarch64 build; the sanitizer is driven through the container's gcc.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly REPO
readonly IMAGE="ubuntu:24.04"
readonly CACHE="xff-bazel-cache"
readonly MODE="${1:-functional}"

docker volume create "${CACHE}" >/dev/null

read -r -d '' SETUP <<'SETUP_EOF' || true
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
apt-get install -y -qq curl gcc g++ python3 unzip zip fuse3 libfuse3-3 >/dev/null
arch="$(uname -m)"; [ "${arch}" = "aarch64" ] && arch=arm64 || arch=amd64
curl -sSL -o /usr/local/bin/bazel "https://github.com/bazelbuild/bazelisk/releases/download/v1.20.0/bazelisk-linux-${arch}"
chmod +x /usr/local/bin/bazel
cd /repo
export XFF_FUSE_REQUIRED=1   # a skipped mount test is a FAILURE here: this machine can mount
SETUP_EOF

if [[ "${MODE}" == "tsan" ]]; then
  script="${SETUP}
sysctl -w vm.mmap_rnd_bits=28 >/dev/null 2>&1 || true
bazel --output_user_root=/bzcache test --config=xff_full //xff/cli:full_extras_test \\
  --copt=-fsanitize=thread --linkopt=-fsanitize=thread --copt=-g --copt=-fno-omit-frame-pointer \\
  --test_env=XFF_FUSE_REQUIRED --nocache_test_results --test_output=all --run_under='setarch -R'"
  # setarch needs to change the process's personality, which the default seccomp profile denies.
  # The array always carries --rm so it is never empty: macOS bash 3.2 treats "${empty[@]}" under
  # `set -u` as an unbound variable and aborts.
  extra=(--rm --privileged)
else
  script="${SETUP}
bazel --output_user_root=/bzcache test --config=xff_full @xff_fuse//... //xff/cli:full_extras_test \\
  --test_env=XFF_FUSE_REQUIRED --nocache_test_results --test_output=errors"
  extra=(--rm)
fi

exec docker run "${extra[@]}" \
  --device /dev/fuse --cap-add SYS_ADMIN --security-opt apparmor:unconfined \
  -v "${REPO}:/repo" -v "${CACHE}:/bzcache" "${IMAGE}" sh -c "${script}"
