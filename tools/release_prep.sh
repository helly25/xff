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
# Release preparation for xff, invoked by .github/workflows/release.yml on a tag
# push. xff uses the override-on-release version model: the git tag is the
# version source of truth, the repository stays at the 0.0.0 sentinel, and this
# script stamps the tag's version into every version location at build time. It:
#
#   1. verifies the tag matches the top CHANGELOG.md heading (so a release always
#      has notes and the version is deliberate);
#   2. stamps <tag> into every in-repo MODULE.bazel version, every intra-repo
#      bazel_dep on one of our modules, and the `xff --version` literal in
#      xff/cli/main.cc;
#   3. runs tools/check_module_versions.py <tag>, so a location the stamp missed
#      fails the release loudly instead of shipping a stale version;
#   4. prints the tag's CHANGELOG.md section to stdout (used as the GitHub release
#      body).
#
# Usage: tools/release_prep.sh <tag>   (tag may also come from GITHUB_REF_NAME)

set -euo pipefail

die() {
  echo "release_prep: ERROR: ${*}" >&2
  exit 1
}

TAG="${1:-${GITHUB_REF_NAME:-}}"
[ -n "${TAG}" ] || die "no tag given (pass as an argument or set GITHUB_REF_NAME)"
[[ "${TAG}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "tag '${TAG}' is not an X.Y.Z version"

# Work from the repository root (this script lives in tools/).
cd "$(dirname "${BASH_SOURCE[0]}")/.."

# 1. The tag must match the newest (top) CHANGELOG heading.
CHANGELOG_VERSION="$(sed -rne 's,^# ([0-9]+([.][0-9]+)+.*)$,\1,p' <CHANGELOG.md | head -n1)"
[ "${CHANGELOG_VERSION}" = "${TAG}" ] \
  || die "tag '${TAG}' does not match the top CHANGELOG.md heading '${CHANGELOG_VERSION}'"

# 2. Stamp the 0.0.0 sentinel -> TAG at every version location. Only our own
#    modules and intra-repo bazel_deps carry version = "0.0.0"; third-party
#    bazel_deps have real versions, so this never rewrites them.
while IFS= read -r module; do
  perl -pi -e "s/version = \"0\\.0\\.0\"/version = \"${TAG}\"/g" "${module}"
done < <(find . -name MODULE.bazel -not -path './bazel-*' -not -path '*/external/*')
# The binary's --version literal: `std::cout << "xff 0.0.0\n";`.
perl -pi -e "s/xff 0\\.0\\.0/xff ${TAG}/g" xff/cli/main.cc

# 3. Fail the release if any version location was missed.
python3 tools/check_module_versions.py "${TAG}"

# 4. Emit the tag's CHANGELOG section (everything under `# <tag>` up to the next
#    version heading) as the release body.
awk -v tag="${TAG}" '
  $0 ~ ("^# " tag "([[:space:]]|$)") { grab = 1; next }
  grab && /^# [0-9]/ { exit }
  grab { print }
' CHANGELOG.md
