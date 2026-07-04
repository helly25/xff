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

}  // namespace
}  // namespace xff::render
