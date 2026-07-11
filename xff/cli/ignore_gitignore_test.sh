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
  expect_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}" # -u force-disables even with -g
  out="$("$(_xff_bin)" -u -g+ "${root}" -type f 2>&1)"
  rm -rf "${root}"
  # ... and even a forced -g+, regardless of position: -u is a master switch, not a
  # participant in the -g last-wins scan.
  expect_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}"
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

test::default_global_git_ignore_is_honored() {
  # git's default global ignore ($XDG_CONFIG_HOME/git/ignore, else ~/.config/git/ignore)
  # is the lowest ignore layer. Drive it via a throwaway HOME (XDG unset).
  local root home out
  root="$(mktemp -d)"
  home="$(mktemp -d)"
  mkdir -p "${root}/.git" "${home}/.config/git"
  touch "${root}/keep.cc" "${root}/drop.bak"
  printf '*.bak\n' >"${home}/.config/git/ignore"
  out="$(env -u XDG_CONFIG_HOME HOME="${home}" "$(_xff_bin)" -g "${root}" -type f 2>&1)"
  rm -rf "${root}" "${home}"
  expect_not_matches "(^|${NL}|/)drop\.bak(\$|${NL})" "${out}" # ~/.config/git/ignore applies
  expect_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}"
}

test::core_excludesfile_is_honored() {
  # core.excludesFile in ~/.gitconfig points at a custom global ignore file.
  local root home out
  root="$(mktemp -d)"
  home="$(mktemp -d)"
  mkdir -p "${root}/.git"
  touch "${root}/keep.cc" "${root}/drop.bak"
  printf '*.bak\n' >"${home}/my_ignore"
  printf '[core]\n\texcludesfile = %s/my_ignore\n' "${home}" >"${home}/.gitconfig"
  out="$(env -u XDG_CONFIG_HOME HOME="${home}" "$(_xff_bin)" -g "${root}" -type f 2>&1)"
  rm -rf "${root}" "${home}"
  expect_not_matches "(^|${NL}|/)drop\.bak(\$|${NL})" "${out}" # core.excludesFile applies
  expect_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}"
}

test::help_topic_documents_gitignore() {
  # Check the exit status separately from the content, so a crashed subprocess (rc != 0,
  # e.g. a sanitizer abort) is distinguishable from a genuine "substring missing"; the
  # content matcher prints the captured output on failure, so no manual diagnostic is needed.
  local out rc
  out="$("$(_xff_bin)" --help=-g 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_contains 'gitignore' "${out}"
}

# Tree: .gitkeep + keep.cc + build.o, with .gitignore = `*` (ignore everything). A .gitkeep is a
# placeholder that keeps its directory in the repo, so xff always keeps it against the gitignore
# layers (#120); the rest is ignored. --hidden shows the dotfile so the exemption is observable.
_make_gitkeep_tree() {
  local root
  root="$(mktemp -d)"
  mkdir -p "${root}/.git"
  touch "${root}/.gitkeep" "${root}/keep.cc" "${root}/build.o"
  printf '*\n' >"${root}/.gitignore"
  echo "${root}"
}

test::gitignore_always_keeps_gitkeep() {
  local root out
  root="$(_make_gitkeep_tree)"
  out="$("$(_xff_bin)" -g+ --hidden "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_matches "(^|${NL}|/)\.gitkeep(\$|${NL})" "${out}"    # exempt from `*`, always kept
  expect_not_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}" # ignored by `*`
  expect_not_matches "(^|${NL}|/)build\.o(\$|${NL})" "${out}" # ignored by `*`
}

test::explicit_exclude_overrides_gitkeep_exemption() {
  local root out
  root="$(_make_gitkeep_tree)"
  # The gitignore exemption keeps .gitkeep, but an explicit --exclude still wins over it.
  out="$("$(_xff_bin)" -g+ --hidden --exclude='.gitkeep' "${root}" -type f 2>&1)"
  rm -rf "${root}"
  expect_not_matches "(^|${NL}|/)\.gitkeep(\$|${NL})" "${out}"
}

# A repo whose .git carries plumbing, alongside a non-git hidden dotfile the user keeps
# (.bazelrc), the tracked .gitignore itself, and a gitignored file. git never lists .git in a
# .gitignore (it excludes its own plumbing implicitly), so -g must drop the whole .git tree while
# leaving the user's other hidden files alone.
_make_repo_with_dotfiles() {
  local root
  root="$(mktemp -d)"
  mkdir -p "${root}/.git/hooks" "${root}/build"
  printf 'ref: refs/heads/main\n' >"${root}/.git/HEAD"
  : >"${root}/.git/hooks/pre-commit.sample"
  printf 'build/\n' >"${root}/.gitignore"
  printf 'build --config=foo\n' >"${root}/.bazelrc"
  : >"${root}/build/out.o"
  : >"${root}/keep.cc"
  echo "${root}"
}

test::dash_g_excludes_gits_own_metadata_tree() {
  local root out
  root="$(_make_repo_with_dotfiles)" # has .git -> bare -g auto-on
  out="$("$(_xff_bin)" -g "${root}" 2>&1)"
  rm -rf "${root}"
  expect_not_matches "(^|${NL}|/)\.git(/|\$|${NL})" "${out}" # git's own dir + all its contents gone
  expect_matches "(^|${NL}|/)\.bazelrc(\$|${NL})" "${out}"   # a non-git hidden file the user keeps: shown
  expect_matches "(^|${NL}|/)\.gitignore(\$|${NL})" "${out}" # the tracked .gitignore file itself: shown
  expect_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}"
  expect_not_matches "(^|${NL}|/)out\.o(\$|${NL})" "${out}" # build/ ignored by .gitignore
}

test::git_metadata_exclusion_is_independent_of_hidden() {
  local root out
  root="$(_make_repo_with_dotfiles)"
  # -g is git mode, not a hidden toggle: even with --hidden forced on, .git stays out (it is git
  # plumbing, not a mere dotfile) while the user's own dotfiles show.
  out="$("$(_xff_bin)" --hidden -g "${root}" 2>&1)"
  rm -rf "${root}"
  expect_not_matches "(^|${NL}|/)\.git(/|\$|${NL})" "${out}"
  expect_matches "(^|${NL}|/)\.bazelrc(\$|${NL})" "${out}"
}

test::git_metadata_shown_when_gitignore_off() {
  local root out
  root="$(_make_repo_with_dotfiles)"
  # Gitignore off (-g-, find-compatible): xff shows everything, including git's .git tree.
  out="$("$(_xff_bin)" -g- "${root}" 2>&1)"
  rm -rf "${root}"
  expect_matches "(^|${NL}|/)\.git(/|\$|${NL})" "${out}"
}

test_runner
