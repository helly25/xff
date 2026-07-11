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

// Unit tests for the real PCRE2 backend, exercised through the shared RegexBackend seam
// (MakePcre2Backend) so they stay module-local - no dependency on the xff core. This target deps
// :pcre2_backend directly (alwayslink), so the factory is always registered here regardless of the
// //xff:xff_pcre flag: Pcre2Available() is true and MakePcre2Backend() yields the real engine. The
// Matcher-level routing (Grammar::kPcre2 -> this backend) is covered by //xff/cli:full_binary_test.
// `manual` (it pulls @pcre2); run in the full CI cell.

#include <memory>
#include <optional>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/regex/backend.h"

namespace xff::regex {
namespace {

using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsTrue;
using ::testing::Optional;
using ::testing::Pair;

struct Pcre2BackendTest : ::testing::Test {};

TEST_F(Pcre2BackendTest, IsAvailableWhenLinked) {
  EXPECT_THAT(Pcre2Available(), IsTrue());  // this target links the backend, so the factory registered
}

TEST_F(Pcre2BackendTest, BackreferencesMatch) {
  // A backreference is the canonical PCRE2-only feature (RE2 rejects it; see //xff/regex:regex_test).
  ASSERT_OK_AND_ASSIGN(const std::unique_ptr<const RegexBackend> backend, MakePcre2Backend("(\\w+) \\1", false));
  EXPECT_THAT(backend->PartialMatch("the the fox"), IsTrue());  // doubled word
  EXPECT_FALSE(backend->PartialMatch("the quick fox"));
}

TEST_F(Pcre2BackendTest, LookaheadMatchesAndFindsSpan) {
  ASSERT_OK_AND_ASSIGN(const std::unique_ptr<const RegexBackend> backend, MakePcre2Backend("foo(?=bar)", false));
  EXPECT_THAT(backend->PartialMatch("foobar"), IsTrue());
  EXPECT_FALSE(backend->PartialMatch("foobaz"));
  EXPECT_THAT(
      backend->FindFirst("x foobar"), Optional(Pair(Eq(2U), Eq(3U))));  // just "foo", the lookahead is zero-width
}

TEST_F(Pcre2BackendTest, FullMatchAnchorsBothEnds) {
  ASSERT_OK_AND_ASSIGN(const std::unique_ptr<const RegexBackend> backend, MakePcre2Backend("a.c", false));
  EXPECT_THAT(backend->FullMatch("abc"), IsTrue());
  EXPECT_FALSE(backend->FullMatch("xabc"));  // anchored: must match the whole string
  EXPECT_FALSE(backend->FullMatch("abcx"));
  EXPECT_THAT(backend->PartialMatch("xabcx"), IsTrue());  // unanchored still matches within
}

TEST_F(Pcre2BackendTest, FullMatchCapturesReturnsGroups) {
  ASSERT_OK_AND_ASSIGN(const std::unique_ptr<const RegexBackend> backend, MakePcre2Backend("(\\w+)@(\\w+)", false));
  const auto captures = backend->FullMatchCaptures("user@host");
  ASSERT_TRUE(captures.has_value());
  EXPECT_THAT(*captures, ElementsAre("user@host", "user", "host"));  // [0]=whole, then groups
  EXPECT_FALSE(backend->FullMatchCaptures("nope").has_value());
}

TEST_F(Pcre2BackendTest, RewriteUsesRe2StyleBackrefs) {
  // The Rewrite contract is RE2 syntax (\1); the backend translates to PCRE2's $1 internally.
  ASSERT_OK_AND_ASSIGN(const std::unique_ptr<const RegexBackend> backend, MakePcre2Backend("(\\w+)@(\\w+)", false));
  EXPECT_THAT(backend->Rewrite("user@host", "\\2.\\1", /*global=*/false), "host.user");
  EXPECT_THAT(backend->Rewrite("a@b c@d", "<\\1>", /*global=*/true), "<a> <c>");
}

TEST_F(Pcre2BackendTest, CaseInsensitiveFolds) {
  ASSERT_OK_AND_ASSIGN(const std::unique_ptr<const RegexBackend> folded, MakePcre2Backend("readme", true));
  EXPECT_THAT(folded->FullMatch("README"), IsTrue());
  ASSERT_OK_AND_ASSIGN(const std::unique_ptr<const RegexBackend> exact, MakePcre2Backend("readme", false));
  EXPECT_FALSE(exact->FullMatch("README"));
}

TEST_F(Pcre2BackendTest, InvalidPatternReturnsInvalidArgument) {
  EXPECT_THAT(MakePcre2Backend("a(b", false), StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace xff::regex
