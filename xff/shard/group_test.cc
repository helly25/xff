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
#include <string>
#include <string_view>
#include <vector>

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
using ::testing::Optional;
using ::testing::SizeIs;

::testing::Matcher<ShardMember> MemberIs(std::int64_t index, std::string_view path) {
  return AllOf(
      Field("index", &ShardMember::index, Eq(index)), Field("path", &ShardMember::path, Eq(path)),
      Field("duplicates", &ShardMember::duplicates, IsEmpty()));
}

std::vector<ShardSet> Group(const std::vector<std::string_view>& names) {
  return GroupShards(names, *Matcher::Make());
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
  EXPECT_TRUE(sets[0].complete);
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
  EXPECT_FALSE(sets[0].complete);
  EXPECT_THAT(sets[0].missing, ElementsAre(1));
  EXPECT_THAT(sets[0].members, SizeIs(2));
}

TEST_F(GroupShardsTest, DedupsSameIndexRegenerationsByTail) {
  // Two files are the same logical shard 0 (they differ only by the hex tail).
  const std::vector<ShardSet> sets = Group({"data-00000-of-00001.bbbbbbbb", "data-00000-of-00001.aaaaaaaa"});
  ASSERT_THAT(sets, SizeIs(1));
  EXPECT_TRUE(sets[0].complete);  // distinct index 0 covers the declared total of 1
  ASSERT_THAT(sets[0].members, SizeIs(1));
  EXPECT_THAT(sets[0].members[0].index, Eq(0));
  EXPECT_THAT(sets[0].members[0].path, Eq("data-00000-of-00001.aaaaaaaa"));  // lexicographically first
  EXPECT_THAT(sets[0].members[0].duplicates, ElementsAre("data-00000-of-00001.bbbbbbbb"));
}

TEST_F(GroupShardsTest, DotNumSetIsCompleteWhenContiguous) {
  const std::vector<ShardSet> sets = Group({"backup.tar.001", "backup.tar.002", "backup.tar.003"});
  ASSERT_THAT(sets, SizeIs(1));
  EXPECT_THAT(sets[0].scheme, Eq(Scheme::kDotNum));
  EXPECT_THAT(sets[0].total, Eq(std::nullopt));
  EXPECT_TRUE(sets[0].complete);
  EXPECT_THAT(sets[0].members, SizeIs(3));
}

TEST_F(GroupShardsTest, DotNumGapIsFlaggedByContiguity) {
  const std::vector<ShardSet> sets = Group({"backup.tar.001", "backup.tar.003"});
  ASSERT_THAT(sets, SizeIs(1));
  EXPECT_FALSE(sets[0].complete);
  EXPECT_THAT(sets[0].missing, ElementsAre(2));
}

TEST_F(GroupShardsTest, SeparateStemsAreSeparateSetsSortedByStem) {
  const std::vector<ShardSet> sets = Group({"zeta-00000-of-00001", "alpha-00000-of-00001"});
  ASSERT_THAT(sets, SizeIs(2));
  EXPECT_THAT(sets[0].stem, Eq("alpha"));
  EXPECT_THAT(sets[1].stem, Eq("zeta"));
}

}  // namespace
}  // namespace xff::shard
