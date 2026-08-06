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

#include "xff/cli/plain_backend.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "xff/cli/help_backend.h"
#include "xff/cli/help_model.h"

namespace xff::cli {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::WithDropIndent;
using ::testing::Eq;

Inline Text(std::string text) {
  return {.style = Inline::Style::kText, .text = std::move(text)};
}

Inline Code(std::string text) {
  return {.style = Inline::Style::kCode, .text = std::move(text)};
}

Inline Ref(std::string label, RefTarget target) {
  return {.style = Inline::Style::kRef, .text = std::move(label), .target = std::move(target)};
}

// ---- PlainRefLocator ----

struct PlainRefLocatorTest : ::testing::Test {};

TEST_F(PlainRefLocatorTest, ResolvesEachKindToItsPlainLocator) {
  EXPECT_THAT(PlainRefLocator({.kind = RefTarget::Kind::kTopic, .id = "fields"}), Eq("--help=fields"));
  EXPECT_THAT(PlainRefLocator({.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"}), Eq("find(1)"));
  EXPECT_THAT(PlainRefLocator({.kind = RefTarget::Kind::kFlag, .id = "--summary"}), Eq("--summary"));
  EXPECT_THAT(PlainRefLocator({.kind = RefTarget::Kind::kPrimary, .id = "-printf"}), Eq("-printf"));
  EXPECT_THAT(PlainRefLocator({.kind = RefTarget::Kind::kUrl, .id = "https://helly25.com"}), Eq("https://helly25.com"));
}

// ---- RenderInlinesPlain ----

struct RenderInlinesPlainTest : ::testing::Test {};

TEST_F(RenderInlinesPlainTest, DropsEmphasisMarkupAndKeepsText) {
  EXPECT_THAT(RenderInlinesPlain({Text("run "), Code("xff"), Text(" now")}), Eq("run xff now"));
}

TEST_F(RenderInlinesPlainTest, RefWithALabelShowsTheLabel) {
  EXPECT_THAT(
      RenderInlinesPlain({Text("see "), Ref("the fields", {.kind = RefTarget::Kind::kTopic, .id = "fields"})}),
      Eq("see the fields"));
}

TEST_F(RenderInlinesPlainTest, RefWithoutALabelShowsItsLocator) {
  EXPECT_THAT(
      RenderInlinesPlain({Text("see "), Ref("", {.kind = RefTarget::Kind::kTopic, .id = "fields"})}),
      Eq("see --help=fields"));
}

// ---- WrapText ----

struct WrapTextTest : ::testing::Test {};

TEST_F(WrapTextTest, WidthZeroEmitsOneUnwrappedIndentedLine) {
  EXPECT_THAT(WrapText("the quick brown fox", 0, "  ", "    "), Eq("  the quick brown fox\n"));
}

TEST_F(WrapTextTest, EmptyTextYieldsEmptyString) {
  EXPECT_THAT(WrapText("", 0, "  ", "  "), Eq(""));
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

// ---- RenderDocument over PlainTextBackend ----

struct PlainBackendTest : ::testing::Test {};

TEST_F(PlainBackendTest, RendersAWholeDocumentInOrder) {
  const Document doc{
      .name = "xff",
      .tagline = "eXtended File Find",
      .usage = "xff [path...] [expr]",
      .sections =
          {
              Section{
                  .title = "DESCRIPTION",
                  .children =
                      {
                          Content{
                              .node =
                                  Prose{
                                      .runs =
                                          {Text("Find files; see "),
                                           Ref("", {.kind = RefTarget::Kind::kTopic, .id = "fields"}), Text(".")}}},
                      },
              },
              Section{
                  .title = "OPTIONS",
                  .children =
                      {
                          Content{
                              .node =
                                  Entry{
                                      .term = "--summary",
                                      .summary = {Text("group + aggregate")},
                                      .details = {Content{.node = Prose{.runs = {Text("more detail.")}}}},
                                      .xff = true}},
                          Content{
                              .node =
                                  Rows{
                                      .rows =
                                          {{.term = "%p", .description = {Text("path")}},
                                           {.term = "%f", .description = {Text("name")}}}}},
                          Content{.node = Bullets{.items = {{Text("first")}, {Text("second")}}}},
                          Content{.node = Example{.text = "xff . -type f", .lang = "sh"}},
                          Content{
                              .node =
                                  SeeAlso{
                                      .refs = {{.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"}},
                                      .note = {Text("the classic.")}}},
                      },
              },
          },
  };

  PlainTextBackend backend;
  RenderDocument(doc, backend);
  EXPECT_THAT(backend.Take(), WithDropIndent(EqualsText(R"out(
      xff - eXtended File Find

      Usage: xff [path...] [expr]

      DESCRIPTION
      Find files; see --help=fields.

      OPTIONS
        --summary [xff]
            group + aggregate
      more detail.
        %p  path
        %f  name
        - first
        - second
          xff . -type f
      See also: find(1) the classic.
      )out")));
}

}  // namespace
}  // namespace xff::cli
