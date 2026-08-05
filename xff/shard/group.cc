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

#include "xff/shard/group.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
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
// filename bucketed by its index (same-index files are duplicate regenerations).
struct Accum {
  int width = 0;
  std::map<std::int64_t, std::vector<std::string>> by_index;
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

}  // namespace

std::vector<ShardSet> GroupShards(absl::Span<const std::string_view> filenames, const Matcher& matcher) {
  std::map<SetKey, Accum> sets;  // ordered, so output is deterministic by (scheme, stem, total)
  for (const std::string_view filename : filenames) {
    const std::optional<Match> match = matcher.Decode(filename);
    if (!match.has_value()) {
      continue;  // not a shard under any scheme
    }
    Accum& accum = sets[SetKey{match->scheme, match->stem, match->total}];
    accum.width = match->width;
    accum.by_index[match->index].emplace_back(filename);
  }

  std::vector<ShardSet> result;
  result.reserve(sets.size());
  for (auto& [key, accum] : sets) {
    ShardSet set{
        .scheme = std::get<0>(key),
        .stem = std::get<1>(key),
        .total = std::get<2>(key),
        .width = accum.width,
    };
    for (auto& [index, paths] : accum.by_index) {
      absl::c_sort(paths);  // deterministic representative + duplicate order
      ShardMember member{.index = index, .path = std::move(paths.front())};
      member.duplicates.assign(paths.begin() + 1, paths.end());
      set.members.push_back(std::move(member));
    }
    // by_index is ordered, so members are already sorted by ascending index.
    ComputeCompleteness(set);
    result.push_back(std::move(set));
  }

  // Present sets stem-first (the user reads by name), then scheme / total.
  absl::c_sort(result, [](const ShardSet& lhs, const ShardSet& rhs) {
    return std::tie(lhs.stem, lhs.scheme, lhs.total) < std::tie(rhs.stem, rhs.scheme, rhs.total);
  });
  return result;
}

}  // namespace xff::shard
