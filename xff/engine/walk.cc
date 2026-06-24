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

#include "xff/engine/walk.h"

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

// Final path component, tolerating a single trailing '/' (but not a lone "/").
std::string_view Basename(std::string_view path) {
  if (path.size() > 1 && path.back() == '/') {
    path.remove_suffix(1);
  }
  const std::string_view::size_type slash = path.rfind('/');
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

class Walker {
 public:
  Walker(const vfs::FileSystem& fs, const WalkOptions& options, Visitor visit, WalkErrorFn on_error)
      : fs_(fs), options_(options), visit_(visit), on_error_(on_error) {}

  void VisitNode(const std::string& path, int depth) {
    if (stopped_) {
      return;
    }
    // -P: never follow; -H: follow only command-line operands (depth 0); -L:
    // follow all. A dangling symlink (target missing) falls back to the link.
    const bool follow =
        options_.symlinks == SymlinkMode::kAll || (options_.symlinks == SymlinkMode::kRoots && depth == 0);
    absl::StatusOr<vfs::Metadata> metadata = fs_.Stat(path, follow);
    if (!metadata.ok() && follow) {
      metadata = fs_.Stat(path, /*follow_symlinks=*/false);
    }
    if (!metadata.ok()) {
      on_error_(path, metadata.status());
      return;
    }

    if (depth == 0) {
      root_dev_ = metadata->dev;  // device of the root this subtree started from (-xdev)
      current_root_ = path;       // command-line operand this subtree descends from (find %H)
    }
    const Visit visit{
        .path = path, .name = Basename(path), .root = current_root_, .depth = depth, .metadata = *metadata};
    const bool is_dir = metadata->type == vfs::FileType::kDirectory;
    const bool within_depth = options_.max_depth < 0 || depth < options_.max_depth;
    const bool visible = depth >= options_.min_depth;
    const bool on_root_fs = !options_.single_filesystem || metadata->dev == root_dev_;
    const bool can_descend = is_dir && within_depth && on_root_fs;

    if (options_.post_order) {
      // find -depth: descend first, then visit. -prune cannot un-visit the
      // already-walked children, so it has no effect here (matching find).
      if (can_descend) {
        MaybeDescend(path, depth, metadata->dev, metadata->ino);
      }
      if (!stopped_ && visible && visit_(visit) == WalkAction::kStop) {
        stopped_ = true;
      }
      return;
    }

    // Pre-order (default): visit first; the visitor may prune the descent or stop.
    WalkAction action = WalkAction::kContinue;
    if (visible) {
      action = visit_(visit);
    }
    if (action == WalkAction::kStop) {
      stopped_ = true;
      return;
    }
    if (can_descend && action != WalkAction::kPrune) {
      MaybeDescend(path, depth, metadata->dev, metadata->ino);
    }
  }

 private:
  // Descends into `path` (a directory we've decided to enter), guarding against
  // filesystem loops: if its (dev, ino) is already on the current descent path --
  // only possible when following symlinks -- report it and do not recurse.
  void MaybeDescend(const std::string& path, int depth, std::uint64_t dev, std::uint64_t ino) {
    if (!ancestors_.insert({dev, ino}).second) {
      on_error_(path, absl::FailedPreconditionError("filesystem loop detected"));
      return;
    }
    Descend(path, depth);
    ancestors_.erase({dev, ino});
  }

  void Descend(const std::string& dir, int depth) {
    absl::StatusOr<std::vector<vfs::Entry>> children = fs_.ReadDir(dir);
    if (!children.ok()) {
      on_error_(dir, children.status());
      return;
    }
    // --sort=name: order siblings by path before visiting. They share `dir` as a
    // prefix, so path order is name order; the result is a deterministic walk.
    if (options_.sort == SortOrder::kName) {
      absl::c_sort(*children, [](const vfs::Entry& a, const vfs::Entry& b) { return a.path < b.path; });
    }
    for (const vfs::Entry& child : *children) {
      if (stopped_) {
        return;
      }
      VisitNode(child.path, depth + 1);
    }
  }

  const vfs::FileSystem& fs_;
  const WalkOptions& options_;
  Visitor visit_;
  WalkErrorFn on_error_;
  bool stopped_ = false;
  std::uint64_t root_dev_ = 0;     // device of the current root subtree (for -xdev)
  std::string_view current_root_;  // command-line operand the current subtree descends from (find %H)
  std::set<std::pair<std::uint64_t, std::uint64_t>> ancestors_;  // (dev,ino) on the descent path (loops)
};

}  // namespace

absl::Status Walk(
    const vfs::FileSystem& fs,
    absl::Span<const std::string> roots,
    const WalkOptions& options,
    Visitor visit,
    WalkErrorFn on_error) {
  Walker walker(fs, options, visit, on_error);
  for (const std::string& root : roots) {
    walker.VisitNode(root, 0);
  }
  return absl::OkStatus();
}

}  // namespace xff::engine
