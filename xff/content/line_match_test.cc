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

#include "xff/content/line_match.h"

#include <string_view>

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::content {
namespace {

using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Matcher;

Matcher<LineMatch> LineIs(std::size_t number, std::string_view text) {
  return AllOf(Field("number", &LineMatch::number, number), Field("text", &LineMatch::text, text));
}

// Matches every line (so the result is the full line inventory: numbers + text).
bool Any(std::string_view /*line*/) {
  return true;
}

struct LineMatchTest : ::testing::Test {};

TEST_F(LineMatchTest, EmptyContentHasNoLines) {
  EXPECT_THAT(CollectLineMatches("", Any), IsEmpty());
}

TEST_F(LineMatchTest, SingleLineWithoutTrailingNewline) {
  EXPECT_THAT(CollectLineMatches("hello", Any), ElementsAre(LineIs(1, "hello")));
}

TEST_F(LineMatchTest, TrailingNewlineDoesNotAddPhantomLine) {
  // "a\nb\n" is two lines, not three -- the byte after the final newline is not a line.
  EXPECT_THAT(CollectLineMatches("a\nb\n", Any), ElementsAre(LineIs(1, "a"), LineIs(2, "b")));
}

TEST_F(LineMatchTest, MissingFinalNewlineStillYieldsLastLine) {
  EXPECT_THAT(CollectLineMatches("a\nb", Any), ElementsAre(LineIs(1, "a"), LineIs(2, "b")));
}

TEST_F(LineMatchTest, BlankInteriorLineIsCounted) {
  EXPECT_THAT(CollectLineMatches("a\n\nb", Any), ElementsAre(LineIs(1, "a"), LineIs(2, ""), LineIs(3, "b")));
}

TEST_F(LineMatchTest, CarriageReturnIsStrippedFromCrlf) {
  // The '\r' of a CRLF is not part of the reported text (so patterns match either).
  EXPECT_THAT(CollectLineMatches("a\r\nb\r\n", Any), ElementsAre(LineIs(1, "a"), LineIs(2, "b")));
}

TEST_F(LineMatchTest, LoneNewlineIsOneEmptyLine) {
  EXPECT_THAT(CollectLineMatches("\n", Any), ElementsAre(LineIs(1, "")));
}

TEST_F(LineMatchTest, ReturnsOnlyMatchingLinesWithTheirNumbers) {
  const std::string_view content = "alpha\nbravo\nalpha again\ncharlie\n";
  const auto has_alpha = [](std::string_view line) { return absl::StrContains(line, "alpha"); };
  EXPECT_THAT(CollectLineMatches(content, has_alpha), ElementsAre(LineIs(1, "alpha"), LineIs(3, "alpha again")));
}

TEST_F(LineMatchTest, NoMatchingLinesIsEmpty) {
  const auto never = [](std::string_view) { return false; };
  EXPECT_THAT(CollectLineMatches("a\nb\nc\n", never), IsEmpty());
}

}  // namespace
}  // namespace xff::content
