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

#include "xff/archive/member_cache.h"

#include <optional>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::archive {
namespace {

using ::testing::Eq;
using ::testing::Optional;

struct MemberCacheTest : ::testing::Test {};

TEST_F(MemberCacheTest, AMissIsNulloptAndAHitIsTheContent) {
  MemberCache cache(64);
  EXPECT_THAT(cache.Get("a"), Eq(std::nullopt));
  cache.Put("a", "alpha");
  EXPECT_THAT(cache.Get("a"), Optional(Eq("alpha")));
  EXPECT_THAT(cache.SizeBytes(), 5U);
}

TEST_F(MemberCacheTest, TheLeastRecentlyUsedEntryIsEvictedFirst) {
  MemberCache cache(10);
  cache.Put("a", "aaaa");
  cache.Put("b", "bbbb");
  // Reading `a` makes `b` the least recently used, so the entry `c` displaces is `b`.
  EXPECT_THAT(cache.Get("a"), Optional(Eq("aaaa")));
  cache.Put("c", "cccc");
  EXPECT_THAT(cache.Get("b"), Eq(std::nullopt));
  EXPECT_THAT(cache.Get("a"), Optional(Eq("aaaa")));
  EXPECT_THAT(cache.Get("c"), Optional(Eq("cccc")));
  EXPECT_THAT(cache.SizeBytes(), 8U);
}

TEST_F(MemberCacheTest, ContentLargerThanTheCapacityIsServedButNeverStored) {
  MemberCache cache(4);
  cache.Put("big", "five5");
  EXPECT_THAT(cache.Get("big"), Eq(std::nullopt));
  EXPECT_THAT(cache.SizeBytes(), 0U);
}

TEST_F(MemberCacheTest, RestoringAKeyReplacesItsContentAndTheAccounting) {
  MemberCache cache(64);
  cache.Put("a", "short");
  cache.Put("a", "a-longer-value");
  EXPECT_THAT(cache.Get("a"), Optional(Eq("a-longer-value")));
  EXPECT_THAT(cache.SizeBytes(), 14U);
}

}  // namespace
}  // namespace xff::archive
