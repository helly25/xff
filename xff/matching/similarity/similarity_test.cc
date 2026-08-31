// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/matching/similarity/similarity.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::similarity {
namespace {

using ::testing::Eq;

struct SimilarityTest : ::testing::Test {};

TEST_F(SimilarityTest, IdenticalWordShinglesScoreOneHundred) {
  EXPECT_THAT(WordShinglePercent("one two three four five six", "one two three four five six", 5), Eq(100));
}

TEST_F(SimilarityTest, JaccardUsesUniqueContiguousShingles) {
  // {one two three four five, two three four five six} versus
  // {one two three four five, two three four five seven}: intersection 1, union 3.
  EXPECT_THAT(WordShinglePercent("one two three four five six", "one two three four five seven", 5), Eq(33));
}

TEST_F(SimilarityTest, TokenizationFoldsAsciiCaseAndSeparatesPunctuation) {
  EXPECT_THAT(WordShinglePercent("Hello, WORLD! again", "hello world again", 3), Eq(100));
}

TEST_F(SimilarityTest, ShortInputsContributeOneWholeInputShingle) {
  EXPECT_THAT(WordShinglePercent("one two", "ONE TWO", 5), Eq(100));
  EXPECT_THAT(WordShinglePercent("one two", "one three", 5), Eq(0));
}

TEST_F(SimilarityTest, EmptyInputsHaveDefinedSetSemantics) {
  EXPECT_THAT(WordShinglePercent("", "", 5), Eq(100));
  EXPECT_THAT(WordShinglePercent("", "one", 5), Eq(0));
  EXPECT_THAT(WordShinglePercent("one", "", 5), Eq(0));
  EXPECT_THAT(WordShinglePercent("one", "one", 0), Eq(0));
}

TEST_F(SimilarityTest, DuplicateShinglesDoNotIncreaseTheirWeight) {
  EXPECT_THAT(WordShinglePercent("a b a b", "a b a", 2), Eq(100));
}

TEST_F(SimilarityTest, WordBoundariesCannotCollideInTheShingleRepresentation) {
  EXPECT_THAT(WordShinglePercent("ab c", "a bc", 2), Eq(0));
}

}  // namespace
}  // namespace xff::similarity
