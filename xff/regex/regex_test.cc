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

#include "xff/regex/regex.h"

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

namespace xff::regex {
namespace {

using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::Ne;
using ::testing::NotNull;

struct RegexTest : ::testing::Test {};

TEST_F(RegexTest, FullMatchAnchorsBothEnds) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile(".*\\.txt", /*case_insensitive=*/false));
  EXPECT_TRUE(matcher.FullMatch("a/b/c.txt"));
  EXPECT_FALSE(matcher.FullMatch("c.txt.bak"));  // trailing text -> not a whole-string match
  EXPECT_FALSE(matcher.FullMatch("c.md"));
}

TEST_F(RegexTest, CaseInsensitiveFoldsCase) {
  ASSERT_OK_AND_ASSIGN(const Matcher folded, Matcher::Compile("readme", /*case_insensitive=*/true));
  EXPECT_TRUE(folded.FullMatch("README"));
  ASSERT_OK_AND_ASSIGN(const Matcher exact, Matcher::Compile("readme", /*case_insensitive=*/false));
  EXPECT_FALSE(exact.FullMatch("README"));
}

TEST_F(RegexTest, InvalidPatternReturnsError) {
  EXPECT_THAT(Matcher::Compile("a(b", /*case_insensitive=*/false), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(RegexTest, FullMatchCapturesReturnsGroups) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("(.*)/([^/]+)\\.(.*)", /*case_insensitive=*/false));
  const auto captures = matcher.FullMatchCaptures("a/b/c.txt");
  ASSERT_TRUE(captures.has_value());
  EXPECT_THAT(*captures, ElementsAre("a/b/c.txt", "a/b", "c", "txt"));  // [0]=whole match, then the 3 groups
  EXPECT_FALSE(matcher.FullMatchCaptures("nomatch").has_value());       // no full match -> nullopt
}

TEST_F(RegexTest, FullMatchCapturesWithNoGroupsReturnsWholeMatchOnly) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("a.c", /*case_insensitive=*/false));
  const auto captures = matcher.FullMatchCaptures("abc");
  ASSERT_TRUE(captures.has_value());
  EXPECT_THAT(*captures, ElementsAre("abc"));  // no groups -> index 0 (whole match) only
}

TEST_F(RegexTest, RewriteReplacesFirstOrAllMatches) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("[0-9]+", /*case_insensitive=*/false));
  EXPECT_THAT(matcher.Rewrite("a12b34", "#", /*global=*/false), "a#b34");  // first match only
  EXPECT_THAT(matcher.Rewrite("a12b34", "#", /*global=*/true), "a#b#");    // every match
  EXPECT_THAT(matcher.Rewrite("abc", "#", /*global=*/true), "abc");        // no match -> unchanged
}

TEST_F(RegexTest, RewriteSupportsBackreferences) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("(\\w+)@(\\w+)", /*case_insensitive=*/false));
  EXPECT_THAT(matcher.Rewrite("user@host", "\\2.\\1", /*global=*/false), "host.user");  // \1/\2 backrefs
}

TEST_F(RegexTest, MatcherCacheCompilesOnceAndReusesByKey) {
  MatcherCache cache;
  const Matcher* const a = cache.GetOrCompile("a.*", /*case_insensitive=*/false);
  ASSERT_THAT(a, NotNull());
  EXPECT_TRUE(a->FullMatch("abc"));
  // The same (pattern, case) returns the very same cached matcher (pointer identity).
  EXPECT_THAT(cache.GetOrCompile("a.*", /*case_insensitive=*/false), Eq(a));
  // A different case flag is a distinct cache entry.
  EXPECT_THAT(cache.GetOrCompile("a.*", /*case_insensitive=*/true), AllOf(NotNull(), Ne(a)));
  // An invalid pattern caches a failure and returns nullptr (a no-match, not retried).
  EXPECT_THAT(cache.GetOrCompile("a(b", /*case_insensitive=*/false), IsNull());
}

}  // namespace
}  // namespace xff::regex
