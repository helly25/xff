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

#ifndef XFF_ENGINE_COLLECT_H_
#define XFF_ENGINE_COLLECT_H_

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/btree_map.h"
#include "xff/engine/walk.h"
#include "xff/parser/ast.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {

// The collection a bare `-collect` (no `=NAME`) adds to. Named rather than special-cased: the
// unnamed form is the default name, so nothing downstream needs an "anonymous bucket" branch.
inline constexpr std::string_view kDefaultCollection = "default";

// One entry held back for a post-walk sink, OWNING everything a reduction reads.
//
// This is not defensive copying: a `Visit` is borrowed by construction (its path / name / root are
// views into the walk's own buffers and its metadata is a reference), so storing one and reading it
// after the walk is a use-after-free. `fs_owner` is the same lesson one level up - inside a mounted
// container the filesystem dies with the dive, which is exactly how --archive-mount raced
// ~ArchiveFileSystem (see Visit::fs_owner in walk.h).
struct CollectedEntry {
  std::string path;
  std::string name;
  std::string root;
  int depth = 0;
  vfs::Metadata metadata;
  const vfs::FileSystem* fs = nullptr;
  std::shared_ptr<const vfs::FileSystem> fs_owner;

  // A Visit borrowing THIS entry's storage, for handing to the same sink functions the walk feeds.
  // Valid only while this entry lives and stays put (the vector must not have reallocated since).
  [[nodiscard]] Visit AsVisit() const;
};

// The named collections one run gathered. Keyed by name and ordered by it, so a run that reduces
// several collections reports them in a stable order regardless of walk order or thread timing.
class Collections {
 public:
  // Appends `visit` to `name`, copying what the entry needs to outlive the walk.
  void Add(std::string_view name, const Visit& visit);

  // The collected entries for `name`, or an empty span when nothing collected under it (which is
  // the honest answer for a `-collect` in a branch that never ran).
  [[nodiscard]] const std::vector<CollectedEntry>& Entries(std::string_view name) const;

  // The collection names that actually received an entry, in name order.
  [[nodiscard]] std::vector<std::string_view> Names() const;

  [[nodiscard]] bool Empty() const { return by_name_.empty(); }

  // Entries across every collection. An entry collected under two names counts twice, because it
  // was collected twice.
  [[nodiscard]] std::size_t Size() const;

 private:
  absl::btree_map<std::string, std::vector<CollectedEntry>> by_name_;
};

// The collection name each `-collect` node in `expr` writes to, in AST order, with duplicates
// intact: the driver needs the repetition to apply the duplicate-name rule, and the count to know
// whether any `-collect` is present at all. A bare `-collect` reports kDefaultCollection.
[[nodiscard]] std::vector<std::string_view> CollectionNames(const parser::Expr& expr);

}  // namespace xff::engine

#endif  // XFF_ENGINE_COLLECT_H_
