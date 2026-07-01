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

#include "xff/ignore/ignore.h"

#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::ignore {
namespace {

using ::testing::IsFalse;
using ::testing::IsTrue;

struct IgnoreTest : ::testing::Test {
  // Builds a one-pattern list and reports whether `relpath` is ignored (kIgnore).
  static bool Ignored(std::string_view pattern, std::string_view relpath, bool is_dir = false) {
    PatternList list;
    list.Add(pattern);
    return list.Match(relpath, is_dir) == Decision::kIgnore;
  }
};

TEST_F(IgnoreTest, FloatingNameMatchesAtAnyDepth) {
  // A pattern with no '/' matches a file of that name anywhere in the tree.
  EXPECT_THAT(Ignored("foo.txt", "foo.txt"), IsTrue());
  EXPECT_THAT(Ignored("foo.txt", "a/b/foo.txt"), IsTrue());
  EXPECT_THAT(Ignored("foo.txt", "foo.txt.bak"), IsFalse());  // whole-name, not a substring
  EXPECT_THAT(Ignored("foo.txt", "a/foo.txtx"), IsFalse());
}

TEST_F(IgnoreTest, AnchoredPatternMatchesOnlyAtBase) {
  // A leading or embedded '/' anchors to the base directory.
  EXPECT_THAT(Ignored("/foo.txt", "foo.txt"), IsTrue());
  EXPECT_THAT(Ignored("/foo.txt", "a/foo.txt"), IsFalse());  // anchored: not at a deeper level
  EXPECT_THAT(Ignored("a/b", "a/b"), IsTrue());              // embedded '/' anchors too
  EXPECT_THAT(Ignored("a/b", "x/a/b"), IsFalse());
}

TEST_F(IgnoreTest, StarDoesNotCrossDirectories) {
  EXPECT_THAT(Ignored("*.log", "error.log"), IsTrue());
  EXPECT_THAT(Ignored("*.log", "a/b/error.log"), IsTrue());  // floating: any depth
  EXPECT_THAT(Ignored("a/*.log", "a/error.log"), IsTrue());
  EXPECT_THAT(Ignored("a/*.log", "a/b/error.log"), IsFalse());  // '*' stops at '/'
}

TEST_F(IgnoreTest, QuestionMarkMatchesOneNonSlash) {
  EXPECT_THAT(Ignored("file?.txt", "file1.txt"), IsTrue());
  EXPECT_THAT(Ignored("file?.txt", "file.txt"), IsFalse());
  EXPECT_THAT(Ignored("a?b", "a/b"), IsFalse());  // '?' does not match '/'
}

TEST_F(IgnoreTest, CharacterClass) {
  EXPECT_THAT(Ignored("foo[0-9]", "foo3"), IsTrue());
  EXPECT_THAT(Ignored("foo[0-9]", "fooX"), IsFalse());
  EXPECT_THAT(Ignored("foo[!0-9]", "fooX"), IsTrue());  // [! ] negated class
  EXPECT_THAT(Ignored("foo[!0-9]", "foo3"), IsFalse());
}

TEST_F(IgnoreTest, LeadingGlobstarMatchesAnyDirectory) {
  EXPECT_THAT(Ignored("**/foo", "foo"), IsTrue());
  EXPECT_THAT(Ignored("**/foo", "a/b/foo"), IsTrue());
  EXPECT_THAT(Ignored("**/foo/bar", "x/foo/bar"), IsTrue());
  EXPECT_THAT(Ignored("**/foo/bar", "x/foo/baz"), IsFalse());
}

TEST_F(IgnoreTest, TrailingGlobstarMatchesEverythingBelow) {
  EXPECT_THAT(Ignored("logs/**", "logs/a.txt"), IsTrue());
  EXPECT_THAT(Ignored("logs/**", "logs/a/b.txt"), IsTrue());
  EXPECT_THAT(Ignored("logs/**", "logs"), IsFalse());  // matches contents, not `logs` itself
  EXPECT_THAT(Ignored("logs/**", "other/a.txt"), IsFalse());
}

TEST_F(IgnoreTest, MiddleGlobstarSpansDirectories) {
  EXPECT_THAT(Ignored("a/**/b", "a/b"), IsTrue());  // zero directories between
  EXPECT_THAT(Ignored("a/**/b", "a/x/b"), IsTrue());
  EXPECT_THAT(Ignored("a/**/b", "a/x/y/b"), IsTrue());
  EXPECT_THAT(Ignored("a/**/b", "a/x/bc"), IsFalse());
}

TEST_F(IgnoreTest, DirectoryOnlyPatternNeedsADirectory) {
  PatternList list;
  list.Add("build/");
  EXPECT_THAT(list.Match("build", /*is_dir=*/true), Decision::kIgnore);
  EXPECT_THAT(list.Match("build", /*is_dir=*/false), Decision::kDefault);  // a file named build is not ignored
  EXPECT_THAT(list.Match("src/build", /*is_dir=*/true), Decision::kIgnore);
}

TEST_F(IgnoreTest, LiteralDotIsNotAWildcard) {
  EXPECT_THAT(Ignored("a.b", "axb"), IsFalse());  // '.' is literal, not "any char"
  EXPECT_THAT(Ignored("a.b", "a.b"), IsTrue());
}

TEST_F(IgnoreTest, CommentsAndBlankLinesAreSkipped) {
  PatternList list = PatternList::Parse("# a comment\n\n  \n*.tmp\n");
  EXPECT_THAT(list.size(), 1U);
  EXPECT_THAT(list.Match("x.tmp", false), Decision::kIgnore);
}

TEST_F(IgnoreTest, EscapedLeadingHashAndBangAreLiteral) {
  EXPECT_THAT(Ignored("\\#keep", "#keep"), IsTrue());  // `\#` -> a file literally named "#keep"
  EXPECT_THAT(Ignored("\\!keep", "!keep"), IsTrue());
}

TEST_F(IgnoreTest, NegationReincludesAndLastMatchWins) {
  PatternList list = PatternList::Parse("*.log\n!keep.log\n");
  EXPECT_THAT(list.Match("a.log", false), Decision::kIgnore);
  EXPECT_THAT(list.Match("keep.log", false), Decision::kInclude);  // negation re-includes
  // Order matters: a later ignore overrides an earlier negation.
  PatternList reordered = PatternList::Parse("!keep.log\n*.log\n");
  EXPECT_THAT(reordered.Match("keep.log", false), Decision::kIgnore);
}

TEST_F(IgnoreTest, AddNegateStartsAsReinclude) {
  // The `negate` argument backs --include (a re-include glob); a leading '!' flips it.
  PatternList list;
  list.Add("keep.log", /*negate=*/true);
  EXPECT_THAT(list.Match("keep.log", false), Decision::kInclude);
  PatternList flipped;
  flipped.Add("!keep.log", /*negate=*/true);  // '!' flips the re-include back to ignore
  EXPECT_THAT(flipped.Match("keep.log", false), Decision::kIgnore);
}

TEST_F(IgnoreTest, NoMatchIsDefault) {
  PatternList list = PatternList::Parse("*.log\n");
  EXPECT_THAT(list.Match("main.cc", false), Decision::kDefault);
  EXPECT_THAT(list.empty(), IsFalse());
  EXPECT_THAT(PatternList().empty(), IsTrue());
}

TEST_F(IgnoreTest, CrlfLinesAreTolerated) {
  PatternList list = PatternList::Parse("*.log\r\n!keep.log\r\n");
  EXPECT_THAT(list.size(), 2U);
  EXPECT_THAT(list.Match("keep.log", false), Decision::kInclude);
}

}  // namespace
}  // namespace xff::ignore
