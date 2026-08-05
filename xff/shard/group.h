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

#ifndef XFF_SHARD_GROUP_H_
#define XFF_SHARD_GROUP_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/types/span.h"
#include "xff/shard/shard.h"

// Groups decoded shard filenames into logical shard sets (#84 slice B): the
// display + stats half of sharded-file support. Given the filenames in one
// directory it collapses same-set members, dedups redundant regenerations (same
// index, differing only by tail), and surfaces completeness (missing shards).
// The CLI (`--shards`) and stats layer on top. See docs/design.md "Sharded files".
namespace xff::shard {

// One physical position within a set (a distinct index). `path` is the chosen
// representative filename; `duplicates` are the other files at the same index that
// differ only by their opaque tail (redundant regenerations), sorted.
struct ShardMember {
  std::int64_t index = 0;
  std::string path;
  std::vector<std::string> duplicates;

  friend bool operator==(const ShardMember&, const ShardMember&) = default;
};

// A logical shard set: members grouped under one (scheme, stem, total) identity.
// `missing` lists the absent indices (completeness gaps); `complete` is
// `missing.empty()`. When the scheme declares a total the expected indices are
// 0 .. total-1; otherwise completeness is contiguity between the lowest and
// highest present index.
struct ShardSet {
  Scheme scheme = Scheme::kOf;
  std::string stem;
  std::optional<std::int64_t> total;
  int width = 0;  // index digit width, for wildcard rendering (e.g. f-???-of-003)
  std::vector<ShardMember> members;
  std::vector<std::int64_t> missing;
  bool complete = true;

  friend bool operator==(const ShardSet&, const ShardSet&) = default;
};

// Groups `filenames` into logical shard sets with `matcher`. Non-shard names are
// ignored. Sets are sorted by (stem, scheme, total); within a set, members are
// sorted by index and same-index files (differing only by tail) collapse to one
// member - the lexicographically-first path is kept, the rest recorded as its
// duplicates. Completeness counts distinct indices, so redundant tails never mask
// a gap.
[[nodiscard]] std::vector<ShardSet> GroupShards(absl::Span<const std::string_view> filenames, const Matcher& matcher);

}  // namespace xff::shard

#endif  // XFF_SHARD_GROUP_H_
