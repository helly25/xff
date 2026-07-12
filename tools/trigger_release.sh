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
# Cut an xff release. Run locally from a clean `main`:
#
#     tools/trigger_release.sh <version> [next_version]     # e.g. 0.2.0
#
# Derived from helly25/mbo's release trigger, adapted for what xff does
# differently (mbo is older; these are deliberate improvements):
#
#   * override-on-release (see tools/release_prep.sh): the tag is the version
#     source of truth and the repo stays at the 0.0.0 sentinel, so there is NO
#     committed version to compare or bump - the CHANGELOG is the only check;
#   * release tags are v-prefixed (`vX.Y.Z`) for a clean `v*` tag-protection
#     glob, while the stamped version stays bare (`X.Y.Z`);
#   * the latest release is read from the git tags (the immutable release
#     record), and the new version must be strictly greater - so a release can
#     never go backwards even if the CHANGELOG drifts.
#
# It refuses to run unless HEAD is a clean `main` at `origin/main`, the version
# is X.Y.Z matching the top CHANGELOG.md heading, greater than the latest release
# tag, and its `v` tag is unused; dry-runs tools/release_prep.sh and reverts it
# (so a tag is pushed only if the release build would pass); creates and pushes a
# signed `vX.Y.Z` tag (triggering .github/workflows/release.yml); and opens an
# auto-merging PR that prepends the next version's empty CHANGELOG section.
#
# The next version defaults to a MINOR bump (0.2.0 -> 0.3.0), the usual cadence
# for a pre-1.0, feature-driven series; pass `next_version` to override it. The
# result must be a valid X.Y.Z strictly greater than <version>.
#
# Requires git tag signing configured (`git config user.signingkey`), `gh`, and
# push access.

set -euo pipefail

die() {
  echo "trigger_release: ERROR: ${*}" >&2
  exit 1
}

# version_gt A B : succeed iff A > B, comparing X.Y.Z numerically component-wise.
version_gt() {
  local -a a b
  IFS=. read -ra a <<<"${1}"
  IFS=. read -ra b <<<"${2}"
  local i
  for i in 0 1 2; do
    # Base-10 forced so a leading-zero component is never read as octal.
    if ((10#${a[i]:-0} > 10#${b[i]:-0})); then return 0; fi
    if ((10#${a[i]:-0} < 10#${b[i]:-0})); then return 1; fi
  done
  return 1 # equal is not greater
}

# max_version [V...] : echo the greatest X.Y.Z of the arguments (empty if none).
max_version() {
  (($# == 0)) && {
    printf ''
    return
  }
  local max="" v
  for v in "$@"; do
    if [[ -z "${max}" ]] || version_gt "${v}" "${max}"; then max="${v}"; fi
  done
  printf '%s' "${max}"
}

# release_versions : read tag lines on stdin, echo the bare X.Y.Z of each that is
# a v-prefixed release tag (drops the `v`, skips anything that is not X.Y.Z).
release_versions() {
  local tag v
  while IFS= read -r tag; do
    v="${tag#v}"
    [[ "${v}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] && printf '%s\n' "${v}"
  done
}

# next_release_version CURRENT [EXPLICIT] : echo the version to open next.
# Default is a minor bump (X.(Y+1).0); EXPLICIT overrides it. The result must be
# a valid X.Y.Z strictly greater than CURRENT, else die.
next_release_version() {
  local current="${1}" explicit="${2:-}" next
  if [[ -n "${explicit}" ]]; then
    next="${explicit}"
  else
    next="$(awk -F. '{print $1 "." ($2 + 1) ".0"}' <<<"${current}")"
  fi
  [[ "${next}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] \
    || die "next version '${next}' is not numeric X.Y.Z"
  version_gt "${next}" "${current}" \
    || die "next version '${next}' is not greater than the released version '${current}'"
  printf '%s\n' "${next}"
}

# The latest released version from the git tags (bare, empty if none released).
latest_release_version() {
  local -a versions=()
  local v
  while IFS= read -r v; do versions+=("${v}"); done < <(git tag --list 'v*' | release_versions)
  max_version ${versions[@]+"${versions[@]}"}
}

trigger_release_main() {
  [[ ${#} -ge 1 && ${#} -le 2 ]] || die "usage: ${0} <version> [next_version]   (e.g. 0.2.0)"
  local version="${1}" next_arg="${2:-}"
  # Releases are strictly numeric <major>.<minor>.<patch> (the `v` is added here).
  [[ "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] \
    || die "version must be numeric X.Y.Z, no 'v' prefix (got '${version}')"

  cd "$(git rev-parse --show-toplevel)"
  git fetch --quiet origin main

  # Cut releases only from a pristine main that matches the pushed main exactly.
  local branch
  branch="$(git rev-parse --abbrev-ref HEAD)"
  [[ "${branch}" == "main" ]] || die "must be on 'main' (currently on '${branch}')"
  [[ "$(git rev-parse HEAD)" == "$(git rev-parse origin/main)" ]] \
    || die "HEAD ($(git rev-parse --short HEAD)) is not at origin/main ($(git rev-parse --short origin/main)); pull first"
  [[ -z "$(git status --porcelain)" ]] || die "working tree is not clean"

  # The version must match the newest CHANGELOG heading (release_prep enforces the
  # same thing at build time; catch it before tagging).
  local changelog_version
  changelog_version="$(sed -rne 's,^# ([0-9]+([.][0-9]+)+.*)$,\1,p' <CHANGELOG.md | head -n1)"
  [[ "${version}" == "${changelog_version}" ]] \
    || die "version '${version}' does not match the top CHANGELOG.md heading '${changelog_version}'"

  # The new release must be strictly greater than the latest released tag, so a
  # release never goes backwards (the tags are the immutable release record).
  local latest
  latest="$(latest_release_version)"
  if [[ -n "${latest}" ]]; then
    version_gt "${version}" "${latest}" \
      || die "version '${version}' is not greater than the latest release '${latest}'"
  fi
  [[ -z "$(git tag --list "v${version}")" ]] || die "tag 'v${version}' already exists"

  # Decide (and verify) the next version before touching anything remote.
  local next_version
  next_version="$(next_release_version "${version}" "${next_arg}")"

  # Pre-flight: the release workflow stamps <version> into MODULE.bazel + main.cc
  # and verifies consistency. Run that here against the worktree so we never push
  # a tag the release build would reject; revert the stamp afterwards (and on any
  # early exit - the guards above proved the tree is clean, so a plain checkout
  # restores exactly the stamp).
  restore_worktree() { git checkout --quiet -- . 2>/dev/null || true; }
  trap restore_worktree EXIT
  tools/release_prep.sh "${version}" >/dev/null
  restore_worktree
  trap - EXIT

  # Extract the version's CHANGELOG section for the tag annotation body.
  local notes
  notes="$(awk -v tag="${version}" '
    $0 ~ ("^# " tag "([[:space:]]|$)") { grab = 1; next }
    grab && /^# [0-9]/ { exit }
    grab { print }
  ' CHANGELOG.md)"

  # Tag (signed, v-prefixed) and push. The tag push triggers release.yml.
  git tag -s -a "v${version}" -m "Release ${version}" -m "${notes}"
  git push origin "refs/tags/v${version}"
  echo "trigger_release: pushed tag v${version}; release.yml will build and publish."

  # Open the next version's empty CHANGELOG section as an auto-merging PR, so the
  # following release has somewhere to accumulate notes. No MODULE.bazel bump: the
  # repo stays at the 0.0.0 sentinel (override-on-release).
  local next_branch="chore/changelog_${next_version}"
  git checkout -b "${next_branch}"
  # Insert `# <next>` after the two-line SPDX header, above the current top version.
  {
    head -n 2 CHANGELOG.md
    printf '\n# %s\n' "${next_version}"
    tail -n +3 CHANGELOG.md
  } >CHANGELOG.md.tmp
  mv CHANGELOG.md.tmp CHANGELOG.md
  git add CHANGELOG.md
  git commit --quiet -m "Open CHANGELOG section for ${next_version}"
  git push --quiet -u origin "${next_branch}"

  if command -v gh >/dev/null 2>&1; then
    gh pr create \
      --title "Open CHANGELOG section for ${next_version}" \
      --body "Prepends the empty \`# ${next_version}\` CHANGELOG section after releasing ${version}. Created by ${0}." \
      && gh pr merge "${next_branch}" --auto --squash \
      || echo "trigger_release: could not open/arm the ${next_version} PR; open it from '${next_branch}' manually."
  else
    echo "trigger_release: 'gh' not found; branch '${next_branch}' is pushed, create the PR manually."
  fi

  git checkout --quiet main
  echo "trigger_release: done. Watch the release under Actions; the ${next_version} CHANGELOG PR is set to auto-merge."
}

# Run main only when executed, not when sourced (e.g. by the unit test). The
# `${1+"$@"}` form passes the args through but expands to nothing when there are
# none, avoiding bash 3.2's "$@ is unbound" under `set -u` (macOS default shell).
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  trigger_release_main ${1+"$@"}
fi
