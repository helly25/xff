// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
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
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/shard/shard.h"

namespace xff::shard {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Optional;
using ::testing::SizeIs;

::testing::Matcher<ShardMember> MemberIs(std::int64_t index, std::string_view path) {
  return AllOf(
      Field("index", &ShardMember::index, Eq(index)), Field("path", &ShardMember::path, Eq(path)),
      Field("duplicates", &ShardMember::duplicates, IsEmpty()));
}

std::vector<ShardSet> Group(const std::vector<std::string_view>& names) {
  std::vector<ShardFile> files;
  files.reserve(names.size());
  for (const std::string_view name : names) {
    files.push_back(ShardFile{.name = name});
  }
  return GroupShards(files, *Matcher::Make());
}

struct GroupShardsTest : ::testing::Test {};

TEST_F(GroupShardsTest, IgnoresNonShardNames) {
  EXPECT_THAT(Group({"README.md", "notes", "archive.zip"}), IsEmpty());
}

TEST_F(GroupShardsTest, GroupsAnOfSetAndReportsItComplete) {
  const std::vector<ShardSet> sets = Group({"data-00000-of-00003", "data-00002-of-00003", "data-00001-of-00003"});
  ASSERT_THAT(sets, SizeIs(1));
  EXPECT_THAT(sets[0].stem, Eq("data"));
  EXPECT_THAT(sets[0].total, Optional(Eq(3)));
  EXPECT_THAT(sets[0].width, Eq(5));
  EXPECT_THAT(sets[0].wildcard, Eq(absl::StrCat("data-", std::string(5, '?'), "-of-00003")));
  EXPECT_THAT(sets[0].complete, IsTrue());
  EXPECT_THAT(sets[0].missing, IsEmpty());
  EXPECT_THAT(
      sets[0].members, ElementsAreArray({
                           MemberIs(0, "data-00000-of-00003"),
                           MemberIs(1, "data-00001-of-00003"),
                           MemberIs(2, "data-00002-of-00003"),
                       }));
}

TEST_F(GroupShardsTest, FlagsAMissingShardAgainstADeclaredTotal) {
  const std::vector<ShardSet> sets = Group({"data-00000-of-00003", "data-00002-of-00003"});
  ASSERT_THAT(sets, SizeIs(1));
  EXPECT_THAT(sets[0].complete, IsFalse());
  EXPECT_THAT(sets[0].missing, ElementsAre(1));
  EXPECT_THAT(sets[0].members, SizeIs(2));
}

TEST_F(GroupShardsTest, DedupsSameIndexRegenerationsByTail) {
  // Two files are the same logical shard 0 (they differ only by the hex tail).
  const std::vector<ShardSet> sets = Group({"data-00000-of-00001.bbbbbbbb", "data-00000-of-00001.aaaaaaaa"});
  ASSERT_THAT(sets, SizeIs(1));
  EXPECT_THAT(sets[0].complete, IsTrue());  // distinct index 0 covers the declared total of 1
  ASSERT_THAT(sets[0].members, SizeIs(1));
  EXPECT_THAT(sets[0].members[0].index, Eq(0));
  EXPECT_THAT(sets[0].members[0].path, Eq("data-00000-of-00001.aaaaaaaa"));  // lexicographically first
  EXPECT_THAT(sets[0].members[0].duplicates, ElementsAre("data-00000-of-00001.bbbbbbbb"));
  EXPECT_THAT(
      sets[0].superfluous, ElementsAre(AllOf(
                               Field("index", &SuperfluousShard::index, Eq(0)),
                               Field("path", &SuperfluousShard::path, Eq("data-00000-of-00001.bbbbbbbb")),
                               Field("reason", &SuperfluousShard::reason, Eq(SuperfluousReason::kDuplicate)))));
}

TEST_F(GroupShardsTest, ExcludesOutOfRangeIndicesFromACompleteDeclaredSet) {
  const std::vector<ShardSet> sets = Group({"data-00000-of-00002", "data-00001-of-00002", "data-00002-of-00002"});
  ASSERT_THAT(sets, SizeIs(1));
  EXPECT_THAT(sets[0].complete, IsTrue());
  EXPECT_THAT(sets[0].members, ElementsAre(MemberIs(0, "data-00000-of-00002"), MemberIs(1, "data-00001-of-00002")));
  EXPECT_THAT(
      sets[0].superfluous, ElementsAre(AllOf(
                               Field("index", &SuperfluousShard::index, Eq(2)),
                               Field("path", &SuperfluousShard::path, Eq("data-00002-of-00002")),
                               Field("reason", &SuperfluousShard::reason, Eq(SuperfluousReason::kOutOfRange)))));
}

TEST_F(GroupShardsTest, OutOfRangeIndicesCannotMaskMissingDeclaredMembers) {
  const std::vector<ShardSet> sets = Group({"data-00000-of-00002", "data-00002-of-00002"});
  ASSERT_THAT(sets, SizeIs(1));
  EXPECT_THAT(sets[0].complete, IsFalse());
  EXPECT_THAT(sets[0].missing, ElementsAre(1));
  EXPECT_THAT(sets[0].members, ElementsAre(MemberIs(0, "data-00000-of-00002")));
  EXPECT_THAT(sets[0].superfluous, SizeIs(1));
}

TEST_F(GroupShardsTest, MtimeDedupKeepsTheNewestCopy) {
  // The same logical shard 0; `aaaa` sorts first by name but `bbbb` is newer, so kMtime keeps bbbb.
  const std::vector<ShardFile> files = {
      {.name = "data-00000-of-00001.aaaaaaaa", .mtime = 100},
      {.name = "data-00000-of-00001.bbbbbbbb", .mtime = 200},
  };
  const std::vector<ShardSet> sets = GroupShards(files, *Matcher::Make(), Dedup::kMtime);
  ASSERT_THAT(sets, SizeIs(1));
  ASSERT_THAT(sets[0].members, SizeIs(1));
  EXPECT_THAT(sets[0].members[0].path, Eq("data-00000-of-00001.bbbbbbbb"));  // newest wins over name order
  EXPECT_THAT(sets[0].members[0].duplicates, ElementsAre("data-00000-of-00001.aaaaaaaa"));
}

TEST_F(GroupShardsTest, MtimeDedupBreaksEqualTimestampTiesByName) {
  const std::vector<ShardFile> files = {
      {.name = "data-00000-of-00001.bbbbbbbb", .mtime = 100},
      {.name = "data-00000-of-00001.aaaaaaaa", .mtime = 100},
  };
  const std::vector<ShardSet> sets = GroupShards(files, *Matcher::Make(), Dedup::kMtime);
  ASSERT_THAT(sets, SizeIs(1));
  ASSERT_THAT(sets[0].members, SizeIs(1));
  EXPECT_THAT(sets[0].members[0].path, Eq("data-00000-of-00001.aaaaaaaa"));
  EXPECT_THAT(sets[0].members[0].duplicates, ElementsAre("data-00000-of-00001.bbbbbbbb"));
}

TEST_F(GroupShardsTest, DotNumSetIsCompleteWhenContiguous) {
  const std::vector<ShardSet> sets = Group({"backup.tar.001", "backup.tar.002", "backup.tar.003"});
  ASSERT_THAT(sets, SizeIs(1));
  EXPECT_THAT(sets[0].scheme, Eq(Scheme::kDotNum));
  EXPECT_THAT(sets[0].total, Eq(std::nullopt));
  EXPECT_THAT(sets[0].complete, IsTrue());
  EXPECT_THAT(sets[0].members, SizeIs(3));
}

TEST_F(GroupShardsTest, DotNumGapIsFlaggedByContiguity) {
  const std::vector<ShardSet> sets = Group({"backup.tar.001", "backup.tar.003"});
  ASSERT_THAT(sets, SizeIs(1));
  EXPECT_THAT(sets[0].complete, IsFalse());
  EXPECT_THAT(sets[0].missing, ElementsAre(2));
}

TEST_F(GroupShardsTest, SeparateStemsAreSeparateSetsSortedByStem) {
  const std::vector<ShardSet> sets = Group({"zeta-00000-of-00001", "alpha-00000-of-00001"});
  ASSERT_THAT(sets, SizeIs(2));
  EXPECT_THAT(sets[0].stem, Eq("alpha"));
  EXPECT_THAT(sets[1].stem, Eq("zeta"));
}

TEST_F(GroupShardsTest, AggregatesDistinctShardSizesAndFlagsNonUniformMode) {
  const std::vector<ShardFile> files = {
      {.name = "data-00000-of-00002", .size = 100, .mode = 0644},
      {.name = "data-00001-of-00002", .size = 250, .mode = 0600},  // a different mode
  };
  const std::vector<ShardSet> sets = GroupShards(files, *Matcher::Make());
  ASSERT_THAT(sets, SizeIs(1));
  EXPECT_THAT(sets[0].total_size, Eq(350U));
  EXPECT_THAT(sets[0].uniform_mode, IsFalse());
  ASSERT_THAT(sets[0].members, SizeIs(2));
  EXPECT_THAT(sets[0].members[0].size, Eq(100U));
  EXPECT_THAT(sets[0].members[0].mode, Eq(0644U));
}

TEST_F(GroupShardsTest, SizeCountsDistinctShardsNotDuplicateCopies) {
  // The two files are the same logical shard 0 (they differ only by the tail), so
  // the aggregate size counts the one representative, not both copies.
  const std::vector<ShardFile> files = {
      {.name = "d-00000-of-00001.aaaaaaaa", .size = 40, .mode = 0644},
      {.name = "d-00000-of-00001.bbbbbbbb", .size = 40, .mode = 0644},
  };
  const std::vector<ShardSet> sets = GroupShards(files, *Matcher::Make());
  ASSERT_THAT(sets, SizeIs(1));
  EXPECT_THAT(sets[0].total_size, Eq(40U));  // not 80 - the dup copy is excluded
  EXPECT_THAT(sets[0].uniform_mode, IsTrue());
}

}  // namespace
}  // namespace xff::shard
