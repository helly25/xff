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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
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

// One child of a directory, already stat'd by a read job.
struct Stated {
  std::string path;
  vfs::Metadata metadata;
  bool ok = false;
  absl::Status status;
};

// The result of reading one directory: its children stat'd, or a ReadDir error.
using Listing = absl::StatusOr<std::vector<Stated>>;

// A fixed pool of worker threads running leaf read jobs (`readdir` + `lstat`).
// Workers touch only their job's inputs and the (thread-safe) FileSystem and the
// task queue; they never call back into the walk, so no job can wait on another
// and there is no shared walk state to race (the coordinator runs everything
// else on one thread). With zero workers, `Submit` runs the job inline.
class ReadPool {
 public:
  explicit ReadPool(std::size_t workers) {
    threads_.reserve(workers);
    for (std::size_t i = 0; i < workers; ++i) {
      threads_.emplace_back([this] { Run(); });
    }
  }

  ~ReadPool() {
    {
      const absl::MutexLock lock(&mutex_);
      stop_ = true;  // turns Pending() true, so every worker's Await wakes
    }
    for (std::thread& thread : threads_) {
      thread.join();
    }
  }

  ReadPool(const ReadPool&) = delete;
  ReadPool& operator=(const ReadPool&) = delete;

  // Enqueues a read job (or runs it inline with no workers). Caller must NOT
  // hold `mutex_`.
  std::future<Listing> Submit(std::function<Listing()> job) ABSL_LOCKS_EXCLUDED(mutex_) {
    auto task = std::make_shared<std::packaged_task<Listing()>>(std::move(job));
    std::future<Listing> future = task->get_future();
    if (threads_.empty()) {
      (*task)();  // sequential: run inline
      return future;
    }
    const absl::MutexLock lock(&mutex_);
    queue_.emplace([task] { (*task)(); });
    return future;
  }

 private:
  // A job is ready or the pool is stopping. `absl::Mutex::Await` evaluates this
  // with `mutex_` held, so it requires the lock.
  bool Pending() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) { return stop_ || !queue_.empty(); }

  // Worker loop: drain jobs until stopped and the queue is empty. Caller (the
  // worker thread) must NOT hold `mutex_`.
  void Run() ABSL_LOCKS_EXCLUDED(mutex_) {
    for (;;) {
      std::function<void()> job;
      {
        const absl::MutexLock lock(&mutex_, absl::Condition(this, &ReadPool::Pending));
        if (stop_ && queue_.empty()) {
          return;
        }
        job = std::move(queue_.front());
        queue_.pop();
      }
      job();
    }
  }

  mutable absl::Mutex mutex_;
  std::queue<std::function<void()>> queue_ ABSL_GUARDED_BY(mutex_);
  bool stop_ ABSL_GUARDED_BY(mutex_) = false;
  std::vector<std::thread> threads_;  // created in the ctor, joined in the dtor; not shared otherwise
};

class Walker {
 public:
  Walker(const vfs::FileSystem& fs, const WalkOptions& options, Visitor visit, WalkErrorFn on_error)
      : fs_(fs),
        options_(options),
        visit_(visit),
        on_error_(on_error),
        follow_children_(options.symlinks == SymlinkMode::kAll),
        pool_(options.workers > 1 ? options.workers : std::size_t{0}) {}

  void WalkRoots(absl::Span<const std::string> roots) {
    for (const std::string& root : roots) {
      if (stopped_) {
        return;
      }
      // -P never follows, -H follows command-line operands (depth 0), -L all.
      const bool follow = options_.symlinks != SymlinkMode::kNever;
      const Stated stated = StatNode(root, follow);
      root_dev_ = stated.ok ? stated.metadata.dev : 0;
      current_root_ = root;
      VisitSubtree(stated, /*depth=*/0, /*prefetched=*/nullptr);
    }
  }

 private:
  // lstat (or stat, when following) a single path into a Stated, with the
  // dangling-symlink fallback to the link itself.
  Stated StatNode(const std::string& path, bool follow) const {
    absl::StatusOr<vfs::Metadata> metadata = fs_.Stat(path, follow);
    if (!metadata.ok() && follow) {
      metadata = fs_.Stat(path, /*follow_symlinks=*/false);
    }
    if (!metadata.ok()) {
      return Stated{.path = path, .ok = false, .status = metadata.status()};
    }
    return Stated{.path = path, .metadata = *metadata, .ok = true};
  }

  // A read job: list `dir` and stat every child. Pure - safe to run on a worker.
  Listing ReadDir(const std::string& dir) const {
    absl::StatusOr<std::vector<vfs::Entry>> entries = fs_.ReadDir(dir);
    if (!entries.ok()) {
      return entries.status();
    }
    std::vector<Stated> children;
    children.reserve(entries->size());
    for (const vfs::Entry& entry : *entries) {
      children.push_back(StatNode(entry.path, follow_children_));
    }
    return children;
  }

  std::future<Listing> SubmitRead(const std::string& dir) {
    return pool_.Submit([this, dir] { return ReadDir(dir); });
  }

  bool Descendable(const Stated& stated, int depth) const {
    const bool is_dir = stated.ok && stated.metadata.type == vfs::FileType::kDirectory;
    const bool within_depth = options_.max_depth < 0 || depth < options_.max_depth;
    const bool on_root_fs = !options_.single_filesystem || stated.metadata.dev == root_dev_;
    return is_dir && within_depth && on_root_fs;
  }

  // Reports `stated` to the visitor (pre/post order handled by the caller).
  // Returns the visitor's action, or kContinue when the entry is below
  // `min_depth` (traversed but not visited) or failed to stat.
  WalkAction VisitOne(const Stated& stated, int depth) {
    if (!stated.ok) {
      on_error_(stated.path, stated.status);
      return WalkAction::kContinue;
    }
    if (depth < options_.min_depth) {
      return WalkAction::kContinue;
    }
    const Visit visit{
        .path = stated.path,
        .name = Basename(stated.path),
        .root = current_root_,
        .depth = depth,
        .metadata = stated.metadata};
    const WalkAction action = visit_(visit);
    if (action == WalkAction::kStop) {
      stopped_ = true;
    }
    return action;
  }

  static bool IsDir(const Stated& stated) { return stated.ok && stated.metadata.type == vfs::FileType::kDirectory; }

  // Visits `stated` and, if it is a descendable directory, descends into it.
  // Pre-order by default; post-order (`-depth`) descends first, then visits, and
  // `-prune` has no effect (matching find). `prefetched` is the directory's
  // already-submitted listing read (from the parent's batch), or null to read now.
  void VisitSubtree(const Stated& stated, int depth, std::future<Listing>* prefetched) {
    if (stopped_) {
      return;
    }
    const bool descend = Descendable(stated, depth);
    if (options_.post_order) {
      if (descend) {
        Descend(stated, depth, prefetched);
      }
      if (!stopped_) {
        VisitOne(stated, depth);
      }
      return;
    }
    const WalkAction action = VisitOne(stated, depth);
    if (!stopped_ && descend && action != WalkAction::kPrune) {
      Descend(stated, depth, prefetched);
    }
  }

  // Reads `dir` (from its prefetched future, or now) and recurses its children,
  // guarding against filesystem loops (only possible when following symlinks).
  void Descend(const Stated& dir, int depth, std::future<Listing>* prefetched) {
    const std::pair<std::uint64_t, std::uint64_t> id{dir.metadata.dev, dir.metadata.ino};
    if (!ancestors_.insert(id).second) {
      on_error_(dir.path, absl::FailedPreconditionError("filesystem loop detected"));
      return;
    }
    Listing listing = prefetched != nullptr ? prefetched->get() : SubmitRead(dir.path).get();
    if (!listing.ok()) {
      on_error_(dir.path, listing.status());
      ancestors_.erase(id);
      return;
    }
    if (options_.sort != SortOrder::kNone) {
      absl::c_sort(*listing, [](const Stated& a, const Stated& b) { return a.path < b.path; });
    }
    HandleChildren(*listing, depth + 1);
    ancestors_.erase(id);
  }

  // Recurses a directory's (already sorted) children. The sort modes differ only
  // in how a subdirectory's entry is grouped relative to its subtree. Reads for
  // the descendable subdirectories are submitted as a batch up front so the pool
  // overlaps their IO while the coordinator visits in order.
  void HandleChildren(const std::vector<Stated>& children, int depth) {
    // Inline DFS at each entry's position. kTree emits a subtree in its sorted
    // place; post-order (`-depth`) always uses this shape (descend then visit).
    if (options_.sort == SortOrder::kTree || options_.post_order) {
      std::vector<std::future<Listing>> reads = SubmitSubdirReads(children, depth);
      for (std::size_t i = 0; i < children.size(); ++i) {
        if (stopped_) {
          return;
        }
        VisitSubtree(children[i], depth, reads[i].valid() ? &reads[i] : nullptr);
      }
      return;
    }
    if (options_.sort == SortOrder::kSubtree) {
      // Non-directory entries first (sorted block), then each subtree contiguous.
      for (const Stated& child : children) {
        if (stopped_) {
          return;
        }
        if (!IsDir(child)) {
          VisitOne(child, depth);
        }
      }
      std::vector<std::future<Listing>> reads = SubmitSubdirReads(children, depth);
      for (std::size_t i = 0; i < children.size(); ++i) {
        if (stopped_) {
          return;
        }
        if (IsDir(children[i])) {
          VisitSubtree(children[i], depth, reads[i].valid() ? &reads[i] : nullptr);
        }
      }
      return;
    }
    // kDir / kNone: emit the whole listing block, then recurse the subdirectories.
    // The per-child action is captured so a pruned directory is not descended into.
    std::vector<bool> pruned(children.size(), false);
    for (std::size_t i = 0; i < children.size(); ++i) {
      if (stopped_) {
        return;
      }
      pruned[i] = VisitOne(children[i], depth) == WalkAction::kPrune;
    }
    std::vector<std::future<Listing>> reads = SubmitSubdirReads(children, depth);
    for (std::size_t i = 0; i < children.size(); ++i) {
      if (stopped_) {
        return;
      }
      if (Descendable(children[i], depth) && !pruned[i]) {
        Descend(children[i], depth, reads[i].valid() ? &reads[i] : nullptr);  // entry already visited above
      }
    }
  }

  // Submits a read for every descendable subdirectory in `children`, returning a
  // vector aligned with `children` (an invalid future where there is no read), so
  // the pool overlaps their IO. The sequential walk (no workers) reads lazily at
  // descend time instead, so it never pre-reads siblings it might not reach.
  std::vector<std::future<Listing>> SubmitSubdirReads(const std::vector<Stated>& children, int depth) {
    std::vector<std::future<Listing>> reads(children.size());
    if (options_.workers <= 1) {
      return reads;
    }
    for (std::size_t i = 0; i < children.size(); ++i) {
      if (Descendable(children[i], depth)) {
        reads[i] = SubmitRead(children[i].path);
      }
    }
    return reads;
  }

  const vfs::FileSystem& fs_;
  const WalkOptions& options_;
  Visitor visit_;
  WalkErrorFn on_error_;
  const bool follow_children_;
  ReadPool pool_;
  bool stopped_ = false;
  std::uint64_t root_dev_ = 0;
  std::string_view current_root_;
  std::set<std::pair<std::uint64_t, std::uint64_t>> ancestors_;
};

}  // namespace

absl::Status Walk(
    const vfs::FileSystem& fs,
    absl::Span<const std::string> roots,
    const WalkOptions& options,
    Visitor visit,
    WalkErrorFn on_error) {
  Walker walker(fs, options, visit, on_error);
  walker.WalkRoots(roots);
  return absl::OkStatus();
}

}  // namespace xff::engine
