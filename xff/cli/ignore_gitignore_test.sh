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
# End-to-end test of -g / --gitignore: per-directory .gitignore files, opt-in (off
# by default, find-compatible), stacked per directory, and disabled by -u. Bare -g is
# the AUTO ternary (respect .gitignore only inside a git repo); -g+/=on force it on
# regardless, -g-/=off force it off. _make_tree plants an empty .git so bare -g sees a repo.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${helly25_bashtest}"

# A real newline for line-anchored expect_matches patterns: expect_matches matches
# the whole text ([[ =~ ]]), so `^`/`$` anchor the whole output, not a line.
# `(^|${NL})X` and `X($|${NL})` restore grep's per-line anchoring.
NL=$'\n'

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# Tree: keep.cc, a.o, sub/b.o, sub/keep.h.  <root>/.gitignore => *.o.  An empty .git
# marks the tree as a git repo, so bare -g (auto) turns .gitignore on. The empty .git
# directory contributes no -type f entries, so it does not perturb the assertions.
_make_tree() {
  local root
  root="$(mktemp -d)"
  mkdir -p "${root}/sub" "${root}/.git"
  touch "${root}/keep.cc" "${root}/a.o" "${root}/sub/b.o" "${root}/sub/keep.h"
  printf '*.o\n' >"${root}/.gitignore"
  echo "${root}"
}

# The same tree WITHOUT a .git, so it is not a git repo (bare -g auto stays off).
_make_tree_no_repo() {
  local root
  root="$(mktemp -d)"
  mkdir -p "${root}/sub"
  touch "${root}/keep.cc" "${root}/a.o" "${root}/sub/b.o" "${root}/sub/keep.h"
  printf '*.o\n' >"${root}/.gitignore"
  echo "${root}"
}

test::gitignore_off_by_default() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}" # .gitignore not consulted without -g
}

test::dash_g_auto_respects_gitignore_recursively_in_a_repo() {
  local root out
  root="$(_make_tree)" # has .git -> auto turns on
  out="$("$(_xff_bin)" -g "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_not_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}"     # dropped at the root
  expect_not_matches "(^|${NL}|/)sub/b\.o(\$|${NL})" "${out}" # and in the subdirectory
  expect_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}"
  expect_matches "(^|${NL}|/)sub/keep\.h(\$|${NL})" "${out}"
}

test::dash_g_auto_is_off_outside_a_repo() {
  local root out
  root="$(_make_tree_no_repo)" # no .git -> auto stays off, .gitignore ignored
  out="$("$(_xff_bin)" -g "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}" # not in a repo: bare -g is a no-op
}

test::gitignore_on_forces_even_outside_a_repo() {
  local root out
  root="$(_make_tree_no_repo)" # no .git, but =on forces regardless
  out="$("$(_xff_bin)" --gitignore=on "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_not_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}"
  expect_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}"
}

test::gitignore_off_value_disables() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --gitignore=off "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}"
}

test::dash_g_plus_forces_on_outside_a_repo() {
  local root out
  root="$(_make_tree_no_repo)" # no .git; -g+ is the short form of =on (force on)
  out="$("$(_xff_bin)" -g+ "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_not_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}"
  expect_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}"
}

test::dash_g_minus_forces_off_in_a_repo() {
  local root out
  root="$(_make_tree)" # has .git, so bare -g would auto-on; -g- (=off) forces it off
  out="$("$(_xff_bin)" -g- "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}" # -g- overrides the repo auto-on
}

test::no_ignore_master_switch_overrides_dash_g() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" -g -u "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}" # -u force-disables even with -g
}

test::nested_gitignore_scopes_to_its_subtree() {
  local root out
  root="$(mktemp -d)"
  mkdir -p "${root}/sub"
  touch "${root}/top.tmp" "${root}/sub/inner.tmp"
  printf '*.tmp\n' >"${root}/sub/.gitignore"                   # only in sub/
  out="$("$(_xff_bin)" --gitignore=on "${root}" -type f 2>&1)" # =on: exercise nesting without a .git
  rm -rf "${root}"
  expect_not_matches "(^|${NL}|/)inner\.tmp(\$|${NL})" "${out}" # sub/.gitignore applies here
  expect_matches "(^|${NL}|/)top\.tmp(\$|${NL})" "${out}"       # but not above its directory
}

test::git_info_exclude_is_honored() {
  local root out
  root="$(mktemp -d)"
  mkdir -p "${root}/.git/info" # a real .git dir -> repo root
  printf '*.tmp\n' >"${root}/.git/info/exclude"
  touch "${root}/keep.cc" "${root}/drop.tmp"
  out="$("$(_xff_bin)" -g "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_not_matches "(^|${NL}|/)drop\.tmp(\$|${NL})" "${out}" # .git/info/exclude drops it
  expect_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}"
}

test::dash_g_from_a_subdir_honors_repo_root_ignores() {
  # Search root is a SUBDIR of the repo: git/rg/fd honor the repo-root .gitignore and
  # .git/info/exclude even though they sit ABOVE the search root.
  local root out
  root="$(mktemp -d)"
  mkdir -p "${root}/.git/info" "${root}/sub"
  printf '*.log\n' >"${root}/.gitignore"        # repo-root .gitignore (above the search root)
  printf '*.tmp\n' >"${root}/.git/info/exclude" # repo-level exclude
  touch "${root}/sub/a.log" "${root}/sub/b.tmp" "${root}/sub/keep.cc"
  out="$("$(_xff_bin)" -g "${root}/sub" -type f 2>&1)"
  rm -rf "${root}"
  expect_not_matches "(^|${NL}|/)a\.log(\$|${NL})" "${out}" # repo-root .gitignore reaches down
  expect_not_matches "(^|${NL}|/)b\.tmp(\$|${NL})" "${out}" # .git/info/exclude reaches down
  expect_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}"
}

test::help_topic_documents_gitignore() {
  # Capture the exit status and the full output separately, and surface both when the
  # assertion fails, so a flake (e.g. the sanitizer aborting the subprocess) is
  # diagnosable in CI instead of masquerading as a bare "substring missing".
  local out rc
  out="$("$(_xff_bin)" --help=-g 2>&1)" && rc=0 || rc=$?
  if [[ "${rc}" != "0" ]] || ! grep -qi 'gitignore' <<<"${out}"; then
    echo "help=-g exited ${rc}; captured output:"
    printf '%s\n' "${out}"
  fi
  expect_eq "0" "${rc}"
  expect_eq "yes" "$(grep -qi 'gitignore' <<<"${out}" && echo yes || echo no)"
}

test_runner
