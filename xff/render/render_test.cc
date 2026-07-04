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

#include "xff/render/render.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::render {
namespace {

struct RenderTest : ::testing::Test {};

TEST_F(RenderTest, PlainAppendsNewline) {
  EXPECT_THAT(Renderer(Format::kPlain).Record("a/b/c"), "a/b/c\n");
}

TEST_F(RenderTest, NulAppendsNulTerminator) {
  EXPECT_THAT(Renderer(Format::kNul).Record("a/b/c"), std::string("a/b/c\0", 6));
}

TEST_F(RenderTest, JsonlEmitsOneObjectPerLine) {
  EXPECT_THAT(Renderer(Format::kJsonl).Record("a/b/c"), "{\"path\":\"a/b/c\"}\n");
}

TEST_F(RenderTest, JsonlEscapesQuotesBackslashAndControls) {
  // a "b \c <tab> d  ->  a \" b \\ c \t d
  EXPECT_THAT(Renderer(Format::kJsonl).Record("a\"b\\c\td"), "{\"path\":\"a\\\"b\\\\c\\td\"}\n");
  // A raw control byte (0x01) becomes a \u escape.
  EXPECT_THAT(Renderer(Format::kJsonl).Record(std::string("x\x01y", 3)), "{\"path\":\"x\\u0001y\"}\n");
}

TEST_F(RenderTest, PlainRawIsTheDefaultEncoding) {
  // kPlain defaults to verbatim bytes (find-compatible): a newline in the name passes
  // through, splitting the record.
  EXPECT_THAT(Renderer(Format::kPlain).Record("a\nb"), "a\nb\n");
}

TEST_F(RenderTest, PlainEscapeCEscapesBackslashAndControls) {
  // --path-encoding=escape: backslash + the common control chars become C escapes.
  EXPECT_THAT(Renderer(Format::kPlain, PathEncoding::kEscape).Record("a\nb\tc\\d"), "a\\nb\\tc\\\\d\n");
  // Other control / DEL bytes use \xNN (upper-case hex); printable + high UTF-8 bytes
  // pass through verbatim.
  EXPECT_THAT(Renderer(Format::kPlain, PathEncoding::kEscape).Record(std::string("x\x01y\x7f", 4)), "x\\x01y\\x7F\n");
  EXPECT_THAT(Renderer(Format::kPlain, PathEncoding::kEscape).Record("caf\xc3\xa9"), "caf\xc3\xa9\n");
}

TEST_F(RenderTest, EscapeAppliesOnlyToPlain) {
  // kNul stays raw (the NUL is the separator); kJsonl always JSON-escapes, both
  // regardless of the path encoding.
  EXPECT_THAT(Renderer(Format::kNul, PathEncoding::kEscape).Record("a\nb"), std::string("a\nb\0", 4));
  EXPECT_THAT(Renderer(Format::kJsonl, PathEncoding::kEscape).Record("a\nb"), "{\"path\":\"a\\nb\"}\n");
}

TEST_F(RenderTest, CsvQuotesOnlyWhenNeededAndDoublesQuotes) {
  EXPECT_THAT(Renderer(Format::kCsv).Record("a/b/c"), "a/b/c\n");      // no special char -> unquoted
  EXPECT_THAT(Renderer(Format::kCsv).Record("a,b"), "\"a,b\"\n");      // comma -> quoted
  EXPECT_THAT(Renderer(Format::kCsv).Record("a\"b"), "\"a\"\"b\"\n");  // quote -> doubled + quoted
  EXPECT_THAT(Renderer(Format::kCsv).Record("a\nb"), "\"a\nb\"\n");    // newline -> quoted (kept literal)
}

TEST_F(RenderTest, TsvEscapesTabNewlineAndBackslash) {
  EXPECT_THAT(Renderer(Format::kTsv).Record("a/b/c"), "a/b/c\n");  // no special char -> verbatim
  EXPECT_THAT(Renderer(Format::kTsv).Record("a\tb\nc\\d"), "a\\tb\\nc\\\\d\n");
}

TEST_F(RenderTest, OnlyTabularFormatsHaveAHeader) {
  EXPECT_THAT(Renderer(Format::kCsv).Header(), "path\n");
  EXPECT_THAT(Renderer(Format::kTsv).Header(), "path\n");
  EXPECT_THAT(Renderer(Format::kPlain).Header(), "");
  EXPECT_THAT(Renderer(Format::kNul).Header(), "");
  EXPECT_THAT(Renderer(Format::kJsonl).Header(), "");
}

TEST_F(RenderTest, EncodeTabularRowJoinsAndEncodesCells) {
  const std::vector<std::string> cells = {"a", "b,c", "d\te"};
  // CSV: comma-join; the "b,c" cell is quoted; a tab is not special in CSV, so it stays.
  EXPECT_THAT(EncodeTabularRow(Format::kCsv, cells), "a,\"b,c\",d\te\n");
  // TSV: tab-join; the interior tab in "d\te" is escaped to \t (a literal comma is fine).
  EXPECT_THAT(EncodeTabularRow(Format::kTsv, cells), "a\tb,c\td\\te\n");
  // Non-tabular formats are not rows.
  EXPECT_THAT(EncodeTabularRow(Format::kPlain, cells), "");
  EXPECT_THAT(EncodeTabularRow(Format::kJsonl, cells), "");
}

// The buffered tabular formats (kAligned / kMarkdown) render the whole table at once via
// RenderTable. EXPECT_EQ for the multiline output: its unified diff beats a matcher here.

TEST_F(RenderTest, RenderTableAlignsColumnsUnderADashedHeaderRule) {
  const std::vector<std::string> header = {"name", "size"};
  const std::vector<std::vector<std::string>> rows = {{"a.txt", "3"}, {"README", "12"}};
  EXPECT_EQ(
      RenderTable(Format::kAligned, header, rows),
      "name    size\n"
      "------  ----\n"
      "a.txt   3\n"
      "README  12\n");
}

TEST_F(RenderTest, RenderTableMarkdownEmitsAGithubTableWithARule) {
  const std::vector<std::string> header = {"name", "size"};
  const std::vector<std::vector<std::string>> rows = {{"README", "12"}, {"a.txt", "3"}};
  EXPECT_EQ(
      RenderTable(Format::kMarkdown, header, rows),
      "| name   | size |\n"
      "| ------ | ---- |\n"
      "| README | 12   |\n"
      "| a.txt  | 3    |\n");
}

TEST_F(RenderTest, RenderTableMarkdownFloorsColumnWidthAtThreeForTheRule) {
  EXPECT_EQ(
      RenderTable(Format::kMarkdown, {"x"}, {{"y"}}),
      "| x   |\n"
      "| --- |\n"
      "| y   |\n");
}

TEST_F(RenderTest, RenderTableMarkdownEscapesInteriorPipes) {
  // A literal `|` in a cell is escaped so it cannot split the column.
  EXPECT_EQ(
      RenderTable(Format::kMarkdown, {"name"}, {{"has|pipe.txt"}}),
      "| name          |\n"
      "| ------------- |\n"
      "| has\\|pipe.txt |\n");
}

TEST_F(RenderTest, RenderTableNoHeaderDropsTheHeaderAndRule) {
  // --no-header: only the data rows, and the widths no longer count the hidden header.
  const std::vector<std::string> header = {"name", "size"};
  const std::vector<std::vector<std::string>> rows = {{"README", "12"}, {"a.txt", "3"}};
  EXPECT_EQ(
      RenderTable(Format::kAligned, header, rows, /*with_header=*/false),
      "README  12\n"
      "a.txt   3\n");
}

TEST_F(RenderTest, RenderTableIsEmptyForTheStreamingAndNonTabularFormats) {
  const std::vector<std::vector<std::string>> rows = {{"a"}};
  EXPECT_THAT(RenderTable(Format::kCsv, {"path"}, rows), "");
  EXPECT_THAT(RenderTable(Format::kTsv, {"path"}, rows), "");
  EXPECT_THAT(RenderTable(Format::kPlain, {"path"}, rows), "");
}

}  // namespace
}  // namespace xff::render
