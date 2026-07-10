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
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Optional;
using ::testing::Pair;

struct RegexTest : ::testing::Test {};

TEST_F(RegexTest, FullMatchAnchorsBothEnds) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile(".*\\.txt", /*case_insensitive=*/false));
  EXPECT_TRUE(matcher.FullMatch("a/b/c.txt"));
  EXPECT_FALSE(matcher.FullMatch("c.txt.bak"));  // trailing text -> not a whole-string match
  EXPECT_FALSE(matcher.FullMatch("c.md"));
}

TEST_F(RegexTest, PartialMatchIsUnanchored) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("c\\.txt", /*case_insensitive=*/false));
  EXPECT_TRUE(matcher.PartialMatch("a/b/c.txt"));  // matches a substring (unlike FullMatch)
  EXPECT_TRUE(matcher.PartialMatch("c.txt.bak"));  // trailing text is fine for a partial match
  EXPECT_FALSE(matcher.PartialMatch("c.md"));      // still must occur somewhere
  EXPECT_FALSE(matcher.FullMatch("a/b/c.txt"));    // the same pattern does not match the whole string
}

TEST_F(RegexTest, FindFirstReturnsLeftmostMatchSpan) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("E[0-9]+", /*case_insensitive=*/false));
  EXPECT_THAT(matcher.FindFirst("code E42 and E7"), Optional(Pair(5, 3)));  // leftmost: E42 at offset 5
  EXPECT_THAT(matcher.FindFirst("no match here"), Eq(std::nullopt));
  EXPECT_THAT(matcher.FindFirst("E9"), Optional(Pair(0, 2)));  // at the very start
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

TEST_F(RegexTest, Re2IsTheExplicitDefaultGrammar) {
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("a.c", /*case_insensitive=*/false, Grammar::kRe2));
  EXPECT_TRUE(matcher.FullMatch("abc"));
}

TEST_F(RegexTest, Pcre2GrammarIsNotBuiltInAndReportsUnimplemented) {
  // No PCRE2 backend is registered in this (lean) test binary, so Compile reports the grammar as
  // unavailable -- a distinct Unimplemented state from an InvalidArgument for a bad pattern, and
  // never a silent fallback to RE2. A full build links the real backend and this succeeds.
  EXPECT_FALSE(Pcre2Available());
  EXPECT_THAT(
      Matcher::Compile("a.c", /*case_insensitive=*/false, Grammar::kPcre2), StatusIs(absl::StatusCode::kUnimplemented));
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

TEST_F(RegexTest, ExactGrammarMatchesLiterally) {
  // kExact is a literal engine: metacharacters are plain text, FullMatch is equality, PartialMatch a
  // substring test. It is a core grammar, always available, and never fails to compile.
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("3.50", /*case_insensitive=*/false, Grammar::kExact));
  EXPECT_TRUE(matcher.FullMatch("3.50"));
  EXPECT_FALSE(matcher.FullMatch("3X50"));   // '.' is literal, not a wildcard
  EXPECT_FALSE(matcher.FullMatch("x3.50"));  // FullMatch is whole-string equality
  EXPECT_TRUE(matcher.PartialMatch("price 3.50 now"));
  EXPECT_FALSE(matcher.PartialMatch("price 3X50"));
}

TEST_F(RegexTest, ExactGrammarCompilesAnyPatternAndFindsTheSpan) {
  // A pattern that is not a valid regex is a fine literal (never an InvalidArgument), and FindFirst
  // reports the literal span (offset + pattern length), 1:1 with the old -grep EXACT substring path.
  ASSERT_OK_AND_ASSIGN(const Matcher matcher, Matcher::Compile("foo(bar", /*case_insensitive=*/false, Grammar::kExact));
  EXPECT_TRUE(matcher.PartialMatch("call foo(bar) now"));
  EXPECT_THAT(matcher.FindFirst("aXfoo(barX"), Optional(Pair(Eq(2U), Eq(7U))));  // "foo(bar" at offset 2, len 7
  EXPECT_THAT(matcher.FindFirst("no match"), Eq(std::nullopt));
  EXPECT_THAT(matcher.Rewrite("x foo(bar y foo(bar", "Z", /*global=*/true), "x Z y Z");  // literal replace
}

TEST_F(RegexTest, ExactGrammarCaseInsensitiveFoldsAsciiCase) {
  ASSERT_OK_AND_ASSIGN(const Matcher folded, Matcher::Compile("Readme", /*case_insensitive=*/true, Grammar::kExact));
  EXPECT_TRUE(folded.FullMatch("README"));
  EXPECT_TRUE(folded.PartialMatch("the readme file"));
  EXPECT_THAT(folded.FindFirst("see README now"), Optional(Pair(Eq(4U), Eq(6U))));  // span in the original text
  ASSERT_OK_AND_ASSIGN(const Matcher exact, Matcher::Compile("Readme", /*case_insensitive=*/false, Grammar::kExact));
  EXPECT_FALSE(exact.FullMatch("README"));
}

}  // namespace
}  // namespace xff::regex
