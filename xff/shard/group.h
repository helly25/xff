// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
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
#include <string_view>
#include <vector>

#include "absl/types/span.h"
#include "xff/shard/shard.h"

// Groups decoded shard files into logical shard sets: the display + stats half of
// sharded-file support. Given the files in one directory it collapses same-set
// members, dedups redundant regenerations (same index, differing only by tail),
// surfaces completeness (missing shards), and carries per-file metadata so a set
// can report its aggregate size and validate that its shards share access rights.
// The CLI (`--shards`) and stats layer on top. See docs/design.md "Sharded files".
namespace xff::shard {

// How same-index duplicates (redundant regenerations, differing only by tail) pick
// their representative shard. kFirst keeps the lexicographically-first name (stable,
// no metadata needed); kMtime keeps the newest by mtime (ties break lexicographically);
// kError picks like kFirst but marks the run: a duplicate is treated as an error by the
// caller. Grouping itself never fails - the CLI decides what kError means for the exit.
enum class Dedup : std::uint8_t { kFirst, kMtime, kError };

// A candidate file for grouping: its name plus the metadata a set needs to aggregate
// size, check access-right consistency across its shards, and pick a representative by
// mtime (kMtime dedup).
struct ShardFile {
  std::string_view name;
  std::uint64_t size = 0;
  std::uint32_t mode = 0;  // st_mode permission/type bits
  std::int64_t mtime = 0;  // modification time (for kMtime dedup); unit is the caller's, compared only

  friend bool operator==(const ShardFile&, const ShardFile&) = default;
};

// One physical position within a set (a distinct index). `path` / `size` / `mode`
// are the chosen representative file's; `duplicates` are the other files at the
// same index that differ only by their opaque tail (redundant regenerations), sorted.
struct ShardMember {
  std::int64_t index = 0;
  std::string path;
  std::uint64_t size = 0;
  std::uint32_t mode = 0;
  std::vector<std::string> duplicates;

  friend bool operator==(const ShardMember&, const ShardMember&) = default;
};

// A logical shard set: members grouped under one (scheme, stem, total) identity.
// `missing` lists the absent indices (completeness gaps); `complete` is
// `missing.empty()`. When the scheme declares a total the expected indices are
// 0 .. total-1; otherwise completeness is contiguity between the lowest and
// highest present index. `total_size` sums the distinct shards (not the redundant
// dup copies); `uniform_mode` is false when the shards disagree on their mode - a
// signal that the set's files do not share consistent access rights.
struct ShardSet {
  Scheme scheme = Scheme::kOf;
  std::string stem;
  std::optional<std::int64_t> total;
  int width = 0;         // index digit width, for wildcard rendering (e.g. f-???-of-003)
  std::string wildcard;  // the set's canonical name with the index field masked (`f-???-of-003`)
  std::vector<ShardMember> members;
  std::vector<std::int64_t> missing;
  bool complete = true;
  std::uint64_t total_size = 0;
  bool uniform_mode = true;

  friend bool operator==(const ShardSet&, const ShardSet&) = default;
};

// Groups `files` into logical shard sets with `matcher`. Non-shard names are
// ignored. Sets are sorted by (stem, scheme, total); within a set, members are
// sorted by index and same-index files (differing only by tail) collapse to one
// member - `dedup` chooses which file is kept (kFirst / kError: the
// lexicographically-first name; kMtime: the newest, ties lexicographic), the rest
// recorded as its duplicates. Completeness counts distinct indices, so redundant
// tails never mask a gap.
[[nodiscard]] std::vector<ShardSet> GroupShards(
    absl::Span<const ShardFile> files,
    const Matcher& matcher,
    Dedup dedup = Dedup::kFirst);

}  // namespace xff::shard

#endif  // XFF_SHARD_GROUP_H_
