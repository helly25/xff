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

#include "xff/fuzzy/fuzzy.h"

#include <array>
#include <optional>
#include <string_view>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::fuzzy {
namespace {

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Lt;
using ::testing::Optional;

struct FuzzyTest : ::testing::Test {};

TEST_F(FuzzyTest, TheCharactersMustAppearInOrder) {
  // The rule in one line: a subsequence, so the gaps are free but the ORDER is not.
  EXPECT_THAT(Matches("tmh", "the/main/header.h", /*fold_case=*/false), IsTrue());
  EXPECT_THAT(Matches("hmt", "the/main/header.h", /*fold_case=*/false), IsFalse());
}

TEST_F(FuzzyTest, EveryPatternCharacterHasToBeThere) {
  EXPECT_THAT(Matches("abc", "a-b-c", /*fold_case=*/false), IsTrue());
  EXPECT_THAT(Matches("abcd", "a-b-c", /*fold_case=*/false), IsFalse());
}

TEST_F(FuzzyTest, ARepeatedCharacterNeedsAsManyOccurrences) {
  // The greedy scan consumes each match, so `aa` needs two `a`s rather than matching one twice.
  EXPECT_THAT(Matches("aa", "banana", /*fold_case=*/false), IsTrue());
  EXPECT_THAT(Matches("aaaa", "banana", /*fold_case=*/false), IsFalse());
}

TEST_F(FuzzyTest, AnEmptyPatternMatchesAnythingAndAnEmptyTextOnlyAnEmptyPattern) {
  // Same as an empty glob: a pattern that constrains nothing matches everything.
  EXPECT_THAT(Matches("", "anything", /*fold_case=*/false), IsTrue());
  EXPECT_THAT(Matches("", "", /*fold_case=*/false), IsTrue());
  EXPECT_THAT(Matches("a", "", /*fold_case=*/false), IsFalse());
}

TEST_F(FuzzyTest, CaseFoldingIsTheCallersChoice) {
  EXPECT_THAT(Matches("RM", "README.md", /*fold_case=*/false), IsTrue());
  EXPECT_THAT(Matches("rm", "README.md", /*fold_case=*/false), IsFalse());
  EXPECT_THAT(Matches("rm", "README.md", /*fold_case=*/true), IsTrue());
  EXPECT_THAT(Matches("RM", "readme.md", /*fold_case=*/true), IsTrue());
}

TEST_F(FuzzyTest, TheWholeTextIsSearchedNotJustItsStart) {
  // An anchored match would be a prefix test; a fuzzy finder is expected to find `deep` in the tail.
  EXPECT_THAT(Matches("deep", "a/very/long/path/deep", /*fold_case=*/false), IsTrue());
}

TEST_F(FuzzyTest, SeparatorsAreOrdinaryCharacters) {
  // A pattern may spell the separator itself, which is what makes `s/f` mean "f under some s".
  EXPECT_THAT(Matches("s/f", "src/file.cc", /*fold_case=*/false), IsTrue());
  EXPECT_THAT(Matches("f/s", "src/file.cc", /*fold_case=*/false), IsFalse());
}

struct FuzzyScoreTest : ::testing::Test {
  // The only thing a score means is "better than that other one", so the assertions are comparisons.
  static int ScoreOf(std::string_view pattern, std::string_view text, bool fold_case = false) {
    const std::optional<int> score = Score(pattern, text, fold_case);
    return score.value_or(-1);
  }
};

TEST_F(FuzzyScoreTest, NoMatchScoresNothingAndAMatchingEmptyPatternScoresZero) {
  // nullopt rather than 0 for "no match", so a matching empty pattern (which constrains nothing and
  // therefore says nothing about quality) is not confused with a failure.
  EXPECT_THAT(Score("zzz", "the_main_header.h", false), Eq(std::nullopt));
  EXPECT_THAT(Score("", "anything", false), Optional(Eq(0)));
  EXPECT_THAT(Score("toolong", "short", false), Eq(std::nullopt));
}

TEST_F(FuzzyScoreTest, PercentNormalizesScoresAcrossPatterns) {
  EXPECT_THAT(Percent("exact", "exact", false), Optional(Eq(100)));
  EXPECT_THAT(Percent("", "anything", false), Optional(Eq(100)));
  EXPECT_THAT(Percent("zzz", "anything", false), Eq(std::nullopt));
  EXPECT_THAT(Percent("foo", "far_out_of", false), Optional(AllOf(Ge(0), Lt(100))));
}

TEST_F(FuzzyScoreTest, WordStartsBeatCharactersBuriedInsideWords) {
  // The whole point of ranking: `tmh` typed as an abbreviation means the initials.
  EXPECT_THAT(ScoreOf("tmh", "the_main_header.h"), Gt(ScoreOf("tmh", "automath.hpp")));
  EXPECT_THAT(ScoreOf("rdme", "README.md", true), Gt(ScoreOf("rdme", "unrelated_me.txt", true)));
}

TEST_F(FuzzyScoreTest, ConsecutiveCharactersBeatScatteredOnes) {
  EXPECT_THAT(ScoreOf("abc", "xabcx"), Gt(ScoreOf("abc", "xaxbxcx")));
}

TEST_F(FuzzyScoreTest, AnEarlierMatchBeatsALaterOne) {
  EXPECT_THAT(ScoreOf("abc", "abc_zzzzzzzz"), Gt(ScoreOf("abc", "zzzzzzzz_abc")));
}

TEST_F(FuzzyScoreTest, MoreMatchedCharactersAlwaysWin) {
  // The per-character weight is far larger than any bonus, so a longer match cannot lose to a
  // shorter one that happens to sit on nicer boundaries.
  EXPECT_THAT(ScoreOf("main", "the_main_header.h"), Gt(ScoreOf("mh", "the_main_header.h")));
}

TEST_F(FuzzyScoreTest, ALaterAlignmentIsTakenWhenItIsTheBetterOne) {
  // `a_ab` holds two alignments of `ab`: the greedy one (the first `a`, then the only `b`, scattered)
  // and the adjacent pair at the end. Taking the better one is the whole reason scoring needs an
  // alignment search rather than the left-to-right scan Matches gets away with - here it must score
  // as well as `_ab` does, where only the good alignment exists.
  EXPECT_THAT(ScoreOf("ab", "a_ab"), Eq(ScoreOf("ab", "x_ab")));
}

TEST_F(FuzzyScoreTest, CamelCaseHumpsCountAsWordStarts) {
  // Folding, because a hump is by definition upper case and the pattern people type is not.
  EXPECT_THAT(ScoreOf("mh", "theMainHeader", true), Gt(ScoreOf("mh", "themainheader", true)));
}

TEST_F(FuzzyScoreTest, FoldingCaseChangesWhatMatchesNotHowItRanks) {
  EXPECT_THAT(Score("TMH", "the_main_header.h", false), Eq(std::nullopt));
  EXPECT_THAT(Score("TMH", "the_main_header.h", true), Optional(Eq(ScoreOf("tmh", "the_main_header.h"))));
}

TEST_F(FuzzyScoreTest, EveryScoredMatchIsAlsoAMatch) {
  // The two entry points must never disagree: whatever Matches accepts, Score must score.
  static constexpr std::array kCases = std::to_array<std::pair<std::string_view, std::string_view>>({
      {"tmh", "the_main_header.h"},
      {"abc", "aabbcc"},
      {"h", "h"},
      {"zz", "z"},
      {"", "anything"},
      {"xyz", "the_main_header.h"},
  });
  for (const auto& [pattern, text] : kCases) {
    EXPECT_THAT(Score(pattern, text, false).has_value(), Eq(Matches(pattern, text, false))) << pattern << " / " << text;
  }
}

}  // namespace
}  // namespace xff::fuzzy
