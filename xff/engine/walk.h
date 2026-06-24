// SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com)
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef XFF_ENGINE_WALK_H_
#define XFF_ENGINE_WALK_H_

#include <string>
#include <string_view>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {

// Which symlinks the walk resolves (stats the target) before testing/descending:
// none (find `-P`, the default), only command-line operands (`-H`), or all
// (`-L`). Following enables filesystem-loop detection.
enum class SymlinkMode { kNever, kRoots, kAll };

// Sibling ordering within each directory (xff's --sort). kNone keeps the
// filesystem's readdir order (find's default, fastest, non-reproducible); kName
// sorts each directory's entries by path so output is deterministic and diffable.
enum class SortOrder { kNone, kName };

// Traversal limits. Parallelism is layered on in a follow-up (design.md
// "Determinism").
struct WalkOptions {
  // Entries shallower than `min_depth` are traversed but not visited (find
  // `-mindepth`). A root operand is depth 0.
  int min_depth = 0;
  // Directories at depth `max_depth` are visited but not descended into; `-1`
  // means unlimited (find `-maxdepth`).
  int max_depth = -1;
  // When true, a directory is visited after its contents instead of before
  // (find `-depth`); `-prune` then has no effect, matching find.
  bool post_order = false;
  // When true, do not descend into a directory on a different device than the
  // walk root it was reached from (find `-xdev`): the mount point is visited but
  // its contents are not.
  bool single_filesystem = false;
  // Which symlinks to resolve before stat/descend (find `-P`/`-H`/`-L`). When a
  // symlink is followed, its target's metadata is reported and a directory target
  // is descended into, with loop detection.
  SymlinkMode symlinks = SymlinkMode::kNever;
  // Sibling ordering within each directory (xff `--sort`); kNone is readdir order.
  SortOrder sort = SortOrder::kNone;
};

// One visited entry handed to the `Visitor`. `path`/`name` reference storage
// owned by the walk for the duration of the call only.
struct Visit {
  std::string_view path;          // path as traversed (root prefix preserved, like find)
  std::string_view name;          // final path component
  std::string_view root;          // command-line search root this entry was reached from (find %H)
  int depth;                      // 0 for a root operand, +1 per directory level
  const vfs::Metadata& metadata;  // lstat of `path`
};

// Visitor control flow, mirroring find: keep traversing, do not descend into
// this directory (`-prune`), or stop the entire walk (`-quit`).
enum class WalkAction { kContinue, kPrune, kStop };

using Visitor = absl::FunctionRef<WalkAction(const Visit&)>;

// Reports a per-path traversal failure (unreadable directory, failed stat, ...).
// The walk continues; the engine maps these to exit code 2 later (design.md
// "Exit-code model").
using WalkErrorFn = absl::FunctionRef<void(std::string_view path, absl::Status status)>;

// Walks `roots` depth-first over `fs` -- pre-order by default, post-order when
// `options.post_order` is set: each entry at depth >= `options.min_depth` is
// passed to `visit`, and directories are descended into while `options.max_depth`
// allows and the visitor did not `kPrune`/`kStop` them (`kPrune` has no effect in
// post-order, as in find). Per-path failures are reported to `on_error` and do
// not abort. Returns `OkStatus` once the walk completes (including a `kStop`).
absl::Status Walk(
    const vfs::FileSystem& fs,
    absl::Span<const std::string> roots,
    const WalkOptions& options,
    Visitor visit,
    WalkErrorFn on_error);

}  // namespace xff::engine

#endif  // XFF_ENGINE_WALK_H_
