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

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::fuzzy {
namespace {

using ::testing::IsFalse;
using ::testing::IsTrue;

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

}  // namespace
}  // namespace xff::fuzzy
