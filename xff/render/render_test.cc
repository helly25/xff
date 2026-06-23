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

}  // namespace
}  // namespace xff::render
