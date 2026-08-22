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

#include "xff/shard/group.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/types/span.h"
#include "xff/shard/shard.h"

namespace xff::shard {
namespace {

// The set identity: files sharing (scheme, stem, total) are the same logical set.
using SetKey = std::tuple<Scheme, std::string, std::optional<std::int64_t>>;

// Accumulates the raw membership of one set before it is finalized: every matched
// file bucketed by its index (same-index files are duplicate regenerations).
struct Accum {
  int width = 0;
  std::string wildcard;  // the masked-index name; identical for every member of the set
  std::map<std::int64_t, std::vector<ShardFile>> by_index;
};

// Fills `out.missing` / `out.complete` from the present indices. With a declared
// total the expected indices are 0 .. total-1; otherwise it is contiguity between
// the lowest and highest present index.
void ComputeCompleteness(ShardSet& out) {
  const auto present = [&out](std::int64_t idx) {
    return absl::c_any_of(out.members, [idx](const ShardMember& member) { return member.index == idx; });
  };
  if (out.total.has_value()) {
    for (std::int64_t idx = 0; idx < *out.total; ++idx) {
      if (!present(idx)) {
        out.missing.push_back(idx);
      }
    }
  } else if (!out.members.empty()) {
    const std::int64_t low = out.members.front().index;
    const std::int64_t high = out.members.back().index;
    for (std::int64_t idx = low + 1; idx < high; ++idx) {
      if (!present(idx)) {
        out.missing.push_back(idx);
      }
    }
  }
  out.complete = out.missing.empty();
}

bool IsOutOfRange(const ShardSet& set, std::int64_t index) {
  return set.total.has_value() && (index < 0 || index >= *set.total);
}

void AppendOutOfRange(ShardSet& set, std::int64_t index, absl::Span<const ShardFile> files) {
  for (const ShardFile& file : files) {
    set.superfluous.push_back(
        {.index = index, .path = std::string(file.name), .reason = SuperfluousReason::kOutOfRange});
  }
}

ShardMember MakeMember(ShardSet& set, std::int64_t index, absl::Span<const ShardFile> files, Dedup dedup) {
  // Pick the representative (its size / mode become the shard's) per `dedup`: kMtime keeps
  // the newest (ties break on the lexicographically-first name, so it stays deterministic);
  // kFirst / kError keep the lexicographically-first name. A single min/max scan, no full sort.
  const auto by_name = [](const ShardFile& lhs, const ShardFile& rhs) { return lhs.name < rhs.name; };
  const auto* const representative =
      dedup == Dedup::kMtime ? absl::c_max_element(
                                   files,
                                   [](const ShardFile& lhs, const ShardFile& rhs) {
                                     return lhs.mtime != rhs.mtime ? lhs.mtime < rhs.mtime : lhs.name > rhs.name;
                                   })
                             : absl::c_min_element(files, by_name);
  ShardMember member{
      .index = index,
      .path = std::string(representative->name),
      .size = representative->size,
      .mode = representative->mode,
  };
  for (const ShardFile& file : files) {
    if (file.name == representative->name) {
      continue;
    }
    member.duplicates.emplace_back(file.name);
    set.superfluous.push_back(
        {.index = index, .path = std::string(file.name), .reason = SuperfluousReason::kDuplicate});
  }
  return member;
}

ShardSet FinalizeSet(const SetKey& key, const Accum& accum, Dedup dedup) {
  ShardSet set{
      .scheme = std::get<0>(key),
      .stem = std::get<1>(key),
      .total = std::get<2>(key),
      .width = accum.width,
      .wildcard = accum.wildcard,
  };
  for (const auto& [index, files] : accum.by_index) {
    if (IsOutOfRange(set, index)) {
      AppendOutOfRange(set, index, files);
      continue;
    }
    ShardMember member = MakeMember(set, index, files, dedup);
    set.total_size += member.size;
    set.members.push_back(std::move(member));
  }
  if (!set.members.empty()) {
    const std::uint32_t mode = set.members.front().mode;
    set.uniform_mode = absl::c_all_of(set.members, [mode](const ShardMember& member) { return member.mode == mode; });
  }
  ComputeCompleteness(set);
  return set;
}

}  // namespace

std::vector<ShardSet> GroupShards(absl::Span<const ShardFile> files, const Matcher& matcher, Dedup dedup) {
  std::map<SetKey, Accum> sets;  // ordered, so output is deterministic by (scheme, stem, total)
  for (const ShardFile& file : files) {
    const std::optional<Match> match = matcher.Decode(file.name);
    if (!match.has_value()) {
      continue;  // not a shard under any scheme
    }
    Accum& accum = sets[SetKey{match->scheme, match->stem, match->total}];
    accum.width = match->width;
    accum.wildcard = match->wildcard;  // identical across the set's members (masked index)
    accum.by_index[match->index].push_back(file);
  }

  std::vector<ShardSet> result;
  result.reserve(sets.size());
  for (const auto& [key, accum] : sets) {
    result.push_back(FinalizeSet(key, accum, dedup));
  }

  // Present sets stem-first (the user reads by name), then scheme / total.
  absl::c_sort(result, [](const ShardSet& lhs, const ShardSet& rhs) {
    return std::tie(lhs.stem, lhs.scheme, lhs.total) < std::tie(rhs.stem, rhs.scheme, rhs.total);
  });
  return result;
}

}  // namespace xff::shard
