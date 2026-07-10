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

#include "xff/glob/glob.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::glob {
namespace {

struct GlobTest : ::testing::Test {};

TEST_F(GlobTest, StarAndQuestionAreSegmentBounded) {
  EXPECT_THAT(GlobToRegex("*.txt"), "[^/]*\\.txt");  // '*' is non-slash, '.' escaped literal
  EXPECT_THAT(GlobToRegex("a?b"), "a[^/]b");         // '?' is a single non-slash
  EXPECT_THAT(GlobToRegex("src/*"), "src/[^/]*");
}

TEST_F(GlobTest, DoubleStarSegmentCrossesDirectories) {
  EXPECT_THAT(GlobToRegex("**/foo"), "(?:.*/)?foo");  // leading `**/`: zero or more directories
  EXPECT_THAT(GlobToRegex("foo/**"), "foo/.*");       // trailing `/**`: everything below
  EXPECT_THAT(GlobToRegex("**"), ".*");               // a bare `**`
  EXPECT_THAT(GlobToRegex("a**b"), "a[^/]*b");        // `**` glued to chars degrades to `*`
}

TEST_F(GlobTest, CharacterClassAndNegation) {
  EXPECT_THAT(GlobToRegex("[abc]"), "[abc]");
  EXPECT_THAT(GlobToRegex("[!a-z]"), "[^a-z]");  // glob negation `[!` -> RE2 `[^`
}

TEST_F(GlobTest, MetacharactersAreEscapedAndEscapesRespected) {
  EXPECT_THAT(GlobToRegex("a.b+c"), "a\\.b\\+c");  // RE2 metacharacters escaped
  EXPECT_THAT(GlobToRegex("a(b)"), "a\\(b\\)");
  EXPECT_THAT(GlobToRegex("a\\*b"), "a\\*b");  // an escaped `*` is a literal asterisk, not a wildcard
}

}  // namespace
}  // namespace xff::glob
