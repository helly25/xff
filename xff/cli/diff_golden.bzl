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

"""Golden tests for `xff -diff=STYLE` output, one committed file per mode.

`diff_golden_test` takes a single input generator (`setup`, a script that populates two parallel
trees `a` and `b` with fixed mtimes) and a `modes` dict mapping each `-diff` STYLE token
(u / c / n / y / none) to its expected-output golden file. Per mode it runs the built `xff` to
walk `a` and diff each file against `b/{relpath}` -- so the test exercises the real feature (xff
*finds* the files and diffs each against its parallel-tree counterpart), not a single pair. The
fixed mtimes plus relative paths make the header verbatim, so the whole per-mode output is a
single reviewable golden, checked with mbo's `diff_test` (a mismatch prints the offending diff).
"""

load("@helly25_mbo//mbo/diff:diff.bzl", "diff_test")

# Run the generator in a temp dir, then walk `a` diffing each match against `b/{relpath}` in
# mode {mode}. `--sort=tree` fixes the traversal order; XFF_CONFIG points off any real .xffrc.
_RUN = (
    "tmp=$$(mktemp -d) && cp $(location {setup}) $$tmp/setup.sh && " +
    "bin=$$(pwd)/$(location //xff/cli:xff) && " +
    "( cd $$tmp && bash setup.sh && " +
    "XFF_CONFIG=/nonexistent $$bin --sort=tree a -type f -diff={mode} 'b/{{relpath}}' ) > $@"
)

def diff_golden_test(name, setup, modes):
    """Golden tests of `xff ... -diff=STYLE 'b/{relpath}'` over a two-tree fixture, per mode.

    Args:
        name:  Base name; each mode becomes `<name>_<mode>_test`.
        setup: The input generator (a script populating trees `a` and `b` with fixed mtimes).
        modes: Dict {style: golden_file}; style is a -diff token (u / c / n / y / none).
    """
    for mode, golden in modes.items():
        actual = "{}_{}.actual".format(name, mode)
        native.genrule(
            name = "{}_{}_gen".format(name, mode),
            testonly = True,
            srcs = [setup],
            outs = [actual],
            tools = ["//xff/cli:xff"],
            cmd = _RUN.format(setup = setup, mode = mode),
        )
        diff_test(
            name = "{}_{}_test".format(name, mode),
            file_old = golden,
            file_new = actual,
        )
