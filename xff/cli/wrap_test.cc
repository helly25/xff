// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
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

#include "xff/cli/wrap.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"

namespace xff::cli {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::WithDropIndent;
using ::testing::Eq;

struct WrapTextTest : ::testing::Test {};

TEST_F(WrapTextTest, WidthZeroAlwaysEmitsOneIndentedLine) {
  EXPECT_THAT(WrapText("the quick brown fox", 0, "  ", "    "), Eq("  the quick brown fox\n"));
  // Even empty, so it byte-reproduces the pre-wrap "indent + text + newline" emit.
  EXPECT_THAT(WrapText("", 0, "  ", "  "), Eq("  \n"));
}

TEST_F(WrapTextTest, PositiveWidthEmptyTextYieldsEmptyString) {
  EXPECT_THAT(WrapText("", 40, "  ", "  "), Eq(""));
}

TEST_F(WrapTextTest, GreedilyPacksWordsUpToWidth) {
  EXPECT_THAT(WrapText("the quick brown fox jumps", 12, "", ""), WithDropIndent(EqualsText(R"out(
      the quick
      brown fox
      jumps
      )out")));
}

TEST_F(WrapTextTest, ContinuationLinesCarryTheContinuationIndent) {
  // The first line sits behind first_indent, wrapped lines behind cont_indent;
  // the budget is width minus the current line's indent, so it is per-level: at
  // width 14 the first line has 12 columns (14 - "- ") and each wrapped line 12
  // columns (14 - "  "), so "gamma delta" (11) fits on one continuation line.
  EXPECT_THAT(WrapText("alpha beta gamma delta", 14, "- ", "  "), WithDropIndent(EqualsText(R"out(
      - alpha beta
        gamma delta
      )out")));
}

TEST_F(WrapTextTest, AWordWiderThanTheBudgetTakesItsOwnLineUnbroken) {
  EXPECT_THAT(WrapText("a supercalifragilistic b", 8, "", ""), WithDropIndent(EqualsText(R"out(
      a
      supercalifragilistic
      b
      )out")));
}

TEST_F(WrapTextTest, CollapsesWhitespaceRunsBetweenWords) {
  EXPECT_THAT(WrapText("one   two\tthree", 40, "", ""), Eq("one two three\n"));
}

TEST_F(WrapTextTest, AnsiEscapesAreZeroWidthForWrapping) {
  // Each word is wrapped in a color escape ("\x1b[36m" ... "\x1b[0m") whose bytes are
  // zero visible columns. At width 11 the three 3-letter words wrap exactly as their
  // uncolored counterparts would ("one two" = 7 cols fits, + "six" = 11 fits): so
  // wrapping is driven by the visible width, not the (much longer) byte length.
  EXPECT_THAT(
      WrapText("\x1b[36mone\x1b[0m \x1b[36mtwo\x1b[0m \x1b[36msix\x1b[0m", 11, "", ""),
      Eq("\x1b[36mone\x1b[0m \x1b[36mtwo\x1b[0m \x1b[36msix\x1b[0m\n"));
}

TEST_F(WrapTextTest, AnsiInIndentCountsAsZeroWidthBudget) {
  // A colored indent spends only its visible columns (the 2 spaces), not its 8 bytes.
  // At width 7 the visible budget is 7 - 2 = 5, so "ab cd" (5 cols) fits on one line;
  // had the 8 escape bytes counted, the budget would be 0 and every word would wrap.
  EXPECT_THAT(WrapText("ab cd", 7, "\x1b[1m  \x1b[0m", "  "), Eq("\x1b[1m  \x1b[0mab cd\n"));
}

}  // namespace
}  // namespace xff::cli
