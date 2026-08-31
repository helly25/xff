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

#include "xff/cli/help_parse.h"

#include <string_view>
#include <variant>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/cli/help_model.h"

namespace xff::cli {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Matcher;
using ::testing::SizeIs;

Matcher<Inline> InlineIs(Inline::Style style, std::string_view text) {
  return AllOf(Field("style", &Inline::style, Eq(style)), Field("text", &Inline::text, Eq(text)));
}

// ---- ParseInline ----

struct ParseInlineTest : ::testing::Test {};

TEST_F(ParseInlineTest, PlainTextIsOneTextRun) {
  EXPECT_THAT(ParseInline("hello world"), ElementsAre(InlineIs(Inline::Style::kText, "hello world")));
}

TEST_F(ParseInlineTest, EmptyIsNoRuns) {
  EXPECT_THAT(ParseInline(""), IsEmpty());
}

TEST_F(ParseInlineTest, SingleBackticksMakeACodeRun) {
  EXPECT_THAT(
      ParseInline("run `xff` now"), ElementsAreArray({
                                        InlineIs(Inline::Style::kText, "run "),
                                        InlineIs(Inline::Style::kCode, "xff"),
                                        InlineIs(Inline::Style::kText, " now"),
                                    }));
}

TEST_F(ParseInlineTest, CodeSpanAtEachEnd) {
  EXPECT_THAT(ParseInline("`a`"), ElementsAre(InlineIs(Inline::Style::kCode, "a")));
  EXPECT_THAT(
      ParseInline("`a` b `c`"), ElementsAreArray({
                                    InlineIs(Inline::Style::kCode, "a"),
                                    InlineIs(Inline::Style::kText, " b "),
                                    InlineIs(Inline::Style::kCode, "c"),
                                }));
}

TEST_F(ParseInlineTest, DoubledBacktickIsALiteralBacktick) {
  EXPECT_THAT(ParseInline("a``b"), ElementsAre(InlineIs(Inline::Style::kText, "a`b")));
}

TEST_F(ParseInlineTest, UnterminatedBacktickStaysLiteral) {
  EXPECT_THAT(ParseInline("a `b"), ElementsAre(InlineIs(Inline::Style::kText, "a `b")));
}

TEST_F(ParseInlineTest, GlobAndRegexCharactersAreLiteral) {
  // The reason we parse only backticks: these would collide with a Markdown-ish syntax.
  EXPECT_THAT(ParseInline("*.txt [a-z] {a,b}"), ElementsAre(InlineIs(Inline::Style::kText, "*.txt [a-z] {a,b}")));
}

// ---- ParseBlocks ----

struct ParseBlocksTest : ::testing::Test {};

TEST_F(ParseBlocksTest, OneParagraphIsOneProse) {
  const Blocks blocks = ParseBlocks("just a paragraph");
  ASSERT_THAT(blocks, SizeIs(1));
  EXPECT_THAT(std::get<Prose>(blocks[0].node).runs, ElementsAre(InlineIs(Inline::Style::kText, "just a paragraph")));
}

TEST_F(ParseBlocksTest, BlankLineSeparatesParagraphs) {
  const Blocks blocks = ParseBlocks("first\n\nsecond");
  ASSERT_THAT(blocks, SizeIs(2));
  EXPECT_THAT(std::get<Prose>(blocks[0].node).runs, ElementsAre(InlineIs(Inline::Style::kText, "first")));
  EXPECT_THAT(std::get<Prose>(blocks[1].node).runs, ElementsAre(InlineIs(Inline::Style::kText, "second")));
}

TEST_F(ParseBlocksTest, WrappedLinesJoinIntoOneParagraph) {
  const Blocks blocks = ParseBlocks("line one\nline two");
  ASSERT_THAT(blocks, SizeIs(1));
  EXPECT_THAT(std::get<Prose>(blocks[0].node).runs, ElementsAre(InlineIs(Inline::Style::kText, "line one line two")));
}

TEST_F(ParseBlocksTest, InlineCodeFlowsIntoTheParagraph) {
  const Blocks blocks = ParseBlocks("see `-printf`");
  ASSERT_THAT(blocks, SizeIs(1));
  EXPECT_THAT(
      std::get<Prose>(blocks[0].node).runs, ElementsAreArray({
                                                InlineIs(Inline::Style::kText, "see "),
                                                InlineIs(Inline::Style::kCode, "-printf"),
                                            }));
}

TEST_F(ParseBlocksTest, FenceIsAVerbatimExampleWithItsLang) {
  const Blocks blocks = ParseBlocks("```sh\nxff . -type f\n```");
  ASSERT_THAT(blocks, SizeIs(1));
  const auto& example = std::get<Example>(blocks[0].node);
  EXPECT_THAT(example.text, Eq("xff . -type f"));
  EXPECT_THAT(example.lang, Eq("sh"));
}

TEST_F(ParseBlocksTest, FenceWithoutLangAndVerbatimBody) {
  const Blocks blocks = ParseBlocks("```\n  indented *literal*\n  second\n```");
  ASSERT_THAT(blocks, SizeIs(1));
  const auto& example = std::get<Example>(blocks[0].node);
  EXPECT_THAT(example.text, Eq("  indented *literal*\n  second"));  // indentation + globs kept verbatim
  EXPECT_THAT(example.lang, IsEmpty());
}

TEST_F(ParseBlocksTest, MixedProseAndFence) {
  const Blocks blocks = ParseBlocks("intro `code`\n\n```\ncmd\n```\n\nmore");
  ASSERT_THAT(blocks, SizeIs(3));
  EXPECT_THAT(
      std::get<Prose>(blocks[0].node).runs, ElementsAreArray({
                                                InlineIs(Inline::Style::kText, "intro "),
                                                InlineIs(Inline::Style::kCode, "code"),
                                            }));
  EXPECT_THAT(std::get<Example>(blocks[1].node).text, Eq("cmd"));
  EXPECT_THAT(std::get<Prose>(blocks[2].node).runs, ElementsAre(InlineIs(Inline::Style::kText, "more")));
}

}  // namespace
}  // namespace xff::cli
