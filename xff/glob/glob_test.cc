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
  EXPECT_THAT(GlobToRegex("[a-z]"), "[a-z]");    // a range passes straight through to RE2
  EXPECT_THAT(GlobToRegex("[!a-z]"), "[^a-z]");  // glob negation `[!` -> RE2 `[^`
}

TEST_F(GlobTest, PosixClassesAndLiteralClosingBracket) {
  // POSIX class expressions pass through verbatim (RE2 supports them); the inner `]` that closes
  // `[:alpha:]` is not mistaken for the class close.
  EXPECT_THAT(GlobToRegex("[[:alpha:]]"), "[[:alpha:]]");
  EXPECT_THAT(GlobToRegex("[[:ascii:][:digit:]]"), "[[:ascii:][:digit:]]");
  EXPECT_THAT(GlobToRegex("![[:space:]]"), "![[:space:]]");  // class after a literal
  // A `]` as the first class member is a literal (POSIX), escaped for RE2.
  EXPECT_THAT(GlobToRegex("[]]"), "[\\]]");
  EXPECT_THAT(GlobToRegex("[!]]"), "[^\\]]");
}

TEST_F(GlobTest, MetacharactersAreEscapedAndEscapesRespected) {
  EXPECT_THAT(GlobToRegex("a.b+c"), "a\\.b\\+c");  // RE2 metacharacters escaped
  EXPECT_THAT(GlobToRegex("a(b)"), "a\\(b\\)");
  EXPECT_THAT(GlobToRegex("a\\*b"), "a\\*b");  // an escaped `*` is a literal asterisk, not a wildcard
}

TEST_F(GlobTest, GlobLeavesBracesLiteral) {
  // The plain GLOB grammar (and the gitignore engine) never expand braces: `{`/`}`/`,` are literals,
  // escaped for RE2. This is why brace expansion is a separate SHGLOB grammar.
  EXPECT_THAT(GlobToRegex("*.{cc,h}"), "[^/]*\\.\\{cc,h\\}");
  EXPECT_THAT(GlobToRegex("a{b}c"), "a\\{b\\}c");
}

TEST_F(GlobTest, ShglobExpandsBraceAlternation) {
  EXPECT_THAT(ShglobToRegex("*.{cc,h}"), "[^/]*\\.(?:cc|h)");        // the motivating case
  EXPECT_THAT(ShglobToRegex("{a,b,c}"), "(?:a|b|c)");                // three alternatives
  EXPECT_THAT(ShglobToRegex("{src,test}/*"), "(?:src|test)/[^/]*");  // an alternative before a `/`
  EXPECT_THAT(ShglobToRegex("f{1,2}.txt"), "f(?:1|2)\\.txt");
}

TEST_F(GlobTest, ShglobAlternativesAreThemselvesTranslated) {
  // Each alternative is SHGLOB-translated: wildcards, classes and nested braces all work inside.
  EXPECT_THAT(ShglobToRegex("{*.cc,[ab]?}"), "(?:[^/]*\\.cc|[ab][^/])");
  EXPECT_THAT(ShglobToRegex("{a,{b,c}d}"), "(?:a|(?:b|c)d)");               // nesting
  EXPECT_THAT(ShglobToRegex("{src,test}/**/x"), "(?:src|test)/(?:.*/)?x");  // `**` inside an alt
  EXPECT_THAT(ShglobToRegex("{a,,b}"), "(?:a||b)");                         // empty alternatives allowed
}

TEST_F(GlobTest, ShglobLeavesDegenerateBracesLiteral) {
  // A comma-less group and an unbalanced `{` are not alternations - they stay literal braces (bash).
  EXPECT_THAT(ShglobToRegex("{a}"), "\\{a\\}");          // no top-level comma
  EXPECT_THAT(ShglobToRegex("a{bc"), "a\\{bc");          // unbalanced `{`
  EXPECT_THAT(ShglobToRegex("\\{a,b\\}"), "\\{a,b\\}");  // escaped braces are literal
  EXPECT_THAT(ShglobToRegex("[{,}]"), "[{,}]");          // `,`/`{`/`}` inside a class are literal
}

}  // namespace
}  // namespace xff::glob
