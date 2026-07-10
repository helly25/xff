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

// Unit tests for the real PCRE2 backend. This target deps //third_party/pcre2:pcre2_backend
// directly (alwayslink), so the factory is always registered here regardless of the //xff:xff_pcre
// flag - Pcre2Available() is true and Matcher::Compile(kPcre2) exercises the actual engine. It is a
// `manual` target (it pulls @pcre2), run in the full CI cell.

#include <optional>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/regex/regex.h"

namespace xff::regex {
namespace {

using ::mbo::testing::StatusIs;
using ::testing::Eq;
using ::testing::IsTrue;
using ::testing::Optional;
using ::testing::Pair;

struct Pcre2BackendTest : ::testing::Test {};

TEST_F(Pcre2BackendTest, IsAvailableWhenLinked) {
  EXPECT_THAT(Pcre2Available(), IsTrue());  // this target links the backend, so the factory registered
}

TEST_F(Pcre2BackendTest, BackreferencesMatch) {
  // A backreference is the canonical PCRE2-only feature: RE2 rejects the pattern outright.
  ASSERT_OK_AND_ASSIGN(
      const Matcher matcher, Matcher::Compile("(\\w+) \\1", /*case_insensitive=*/false, Grammar::kPcre2));
  EXPECT_THAT(matcher.PartialMatch("the the fox"), IsTrue());  // doubled word
  EXPECT_FALSE(matcher.PartialMatch("the quick fox"));
  EXPECT_THAT(
      Matcher::Compile("(\\w+) \\1", /*case_insensitive=*/false, Grammar::kRe2),
      StatusIs(absl::StatusCode::kInvalidArgument));  // RE2 cannot compile it
}

TEST_F(Pcre2BackendTest, LookaheadMatchesAndFindsSpan) {
  ASSERT_OK_AND_ASSIGN(
      const Matcher matcher, Matcher::Compile("foo(?=bar)", /*case_insensitive=*/false, Grammar::kPcre2));
  EXPECT_THAT(matcher.PartialMatch("foobar"), IsTrue());
  EXPECT_FALSE(matcher.PartialMatch("foobaz"));
  EXPECT_THAT(
      matcher.FindFirst("x foobar"), Optional(Pair(Eq(2U), Eq(3U))));  // just "foo", the lookahead is zero-width
}

TEST_F(Pcre2BackendTest, FullMatchAnchorsBothEnds) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("a.c", /*case_insensitive=*/false, Grammar::kPcre2));
  EXPECT_THAT(matcher.FullMatch("abc"), IsTrue());
  EXPECT_FALSE(matcher.FullMatch("xabc"));  // anchored: must match the whole string
  EXPECT_FALSE(matcher.FullMatch("abcx"));
  EXPECT_THAT(matcher.PartialMatch("xabcx"), IsTrue());  // unanchored still matches within
}

TEST_F(Pcre2BackendTest, FullMatchCapturesReturnsGroups) {
  ASSERT_OK_AND_ASSIGN(
      const Matcher matcher, Matcher::Compile("(\\w+)@(\\w+)", /*case_insensitive=*/false, Grammar::kPcre2));
  const auto captures = matcher.FullMatchCaptures("user@host");
  ASSERT_TRUE(captures.has_value());
  EXPECT_THAT(*captures, ::testing::ElementsAre("user@host", "user", "host"));  // [0]=whole, then groups
  EXPECT_FALSE(matcher.FullMatchCaptures("nope").has_value());
}

TEST_F(Pcre2BackendTest, RewriteUsesRe2StyleBackrefs) {
  // The Rewrite contract is RE2 syntax (\1); the backend translates to PCRE2's $1 internally.
  ASSERT_OK_AND_ASSIGN(
      const Matcher matcher, Matcher::Compile("(\\w+)@(\\w+)", /*case_insensitive=*/false, Grammar::kPcre2));
  EXPECT_THAT(matcher.Rewrite("user@host", "\\2.\\1", /*global=*/false), "host.user");
  EXPECT_THAT(matcher.Rewrite("a@b c@d", "<\\1>", /*global=*/true), "<a> <c>");
}

TEST_F(Pcre2BackendTest, CaseInsensitiveFolds) {
  ASSERT_OK_AND_ASSIGN(const Matcher folded, Matcher::Compile("readme", /*case_insensitive=*/true, Grammar::kPcre2));
  EXPECT_THAT(folded.FullMatch("README"), IsTrue());
  ASSERT_OK_AND_ASSIGN(const Matcher exact, Matcher::Compile("readme", /*case_insensitive=*/false, Grammar::kPcre2));
  EXPECT_FALSE(exact.FullMatch("README"));
}

TEST_F(Pcre2BackendTest, InvalidPatternReturnsInvalidArgument) {
  EXPECT_THAT(
      Matcher::Compile("a(b", /*case_insensitive=*/false, Grammar::kPcre2),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace xff::regex
