#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
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
# End-to-end test of --sort (none/dir/subtree/tree) and -j / --jobs, the CLI gap that was the
# last remaining piece of #43/#27 (the engine unit tests already cover ordering across worker
# counts). Asserts: every mode walks the whole tree; --sort=tree is a fully deterministic global
# order that is identical regardless of -j (the core parallel-determinism guarantee); --sort=dir
# orders each directory's entries; and --jobs=all / -j N visit everything.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# A small tree: root + two directories (b, x) + five files. Eight entries in all.
_make_tree() {
  local dir="$1"
  mkdir -p "${dir}/b" "${dir}/x"
  : >"${dir}/a.txt"
  : >"${dir}/c.txt"
  : >"${dir}/b/d.txt"
  : >"${dir}/b/e.txt"
  : >"${dir}/x/y.txt"
}

# Run xff from inside DIR on '.', config-free, so paths are stable './...' forms.
_walk() {
  local dir="$1"
  shift
  (cd "${dir}" && XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "$@" .)
}

test::every_sort_mode_walks_the_whole_tree() {
  local dir mode
  dir="$(test_tmpdir sortall)"
  _make_tree "${dir}"
  for mode in none dir subtree tree; do
    expect_eq "8" "$(_walk "${dir}" "--sort=${mode}" | wc -l | tr -d ' ')"
  done
}

test::sort_tree_is_a_deterministic_global_order() {
  local dir out expected
  dir="$(test_tmpdir sorttree)"
  _make_tree "${dir}"
  out="$(_walk "${dir}" --sort=tree -j1)"
  expected="$(printf '.\n./a.txt\n./b\n./b/d.txt\n./b/e.txt\n./c.txt\n./x\n./x/y.txt')"
  expect_eq "${expected}" "${out}"
}

test::sort_tree_is_identical_across_worker_counts() {
  local dir
  dir="$(test_tmpdir sortpar)"
  _make_tree "${dir}"
  # The core #43 guarantee: --sort=tree is reproducible regardless of parallelism.
  expect_eq "$(_walk "${dir}" --sort=tree -j1)" "$(_walk "${dir}" --sort=tree -j8)"
}

test::sort_dir_orders_each_directory() {
  local dir out expected
  dir="$(test_tmpdir sortdir)"
  _make_tree "${dir}"
  out="$(_walk "${dir}" --sort=dir -j1)"
  expected="$(printf '.\n./a.txt\n./b\n./c.txt\n./x\n./b/d.txt\n./b/e.txt\n./x/y.txt')"
  expect_eq "${expected}" "${out}"
}

test::jobs_all_and_n_visit_the_whole_tree() {
  local dir
  dir="$(test_tmpdir sortjobs)"
  _make_tree "${dir}"
  expect_eq "8" "$(_walk "${dir}" --jobs=all | wc -l | tr -d ' ')"
  expect_eq "8" "$(_walk "${dir}" -j4 --sort=none | wc -l | tr -d ' ')"
}

test_runner
