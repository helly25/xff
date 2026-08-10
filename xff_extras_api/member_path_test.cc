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

#include "xff/archive/member_path.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::archive {
namespace {

using ::testing::Eq;
using ::testing::Field;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Optional;

testing::Matcher<MemberPathParts> PartsAre(std::string_view container, std::string_view member) {
  return ::testing::AllOf(
      Field("container", &MemberPathParts::container, container), Field("member", &MemberPathParts::member, member));
}

struct MemberPathTest : ::testing::Test {};

TEST_F(MemberPathTest, TheDefaultSeparatorIsTheJarStyleBang) {
  EXPECT_THAT(JoinMemberPath("a.tgz", "inner/x"), "a.tgz!inner/x");
}

TEST_F(MemberPathTest, AnAbsoluteMemberKeepsItsOwnLeadingSlash) {
  // Rendering is plain concatenation: xff adds and removes no slash, because an archive may
  // legitimately store an absolute member and hiding that would hide the Zip-Slip red flag.
  EXPECT_THAT(JoinMemberPath("a.tgz", "/rooted"), "a.tgz!/rooted");
  // With a `!/` separator the same rule produces a doubled slash, which is correct - and is the
  // reason plain `!` is the better default.
  EXPECT_THAT(JoinMemberPath("a.tgz", "/rooted", {.separator = "!/"}), "a.tgz!//rooted");
  EXPECT_THAT(JoinMemberPath("a.tgz", "relative", {.separator = "!/"}), "a.tgz!/relative");
}

TEST_F(MemberPathTest, AnyStringWorksAsASeparatorNotAFixedMenu) {
  // The ecosystem uses several spellings and xff has to be able to emit what another tool accepts.
  EXPECT_THAT(JoinMemberPath("a.tgz", "x", {.separator = "#"}), "a.tgz#x");
  EXPECT_THAT(JoinMemberPath("a.tgz", "x", {.separator = "#/"}), "a.tgz#/x");
  EXPECT_THAT(JoinMemberPath("a.tgz", "x", {.separator = "::"}), "a.tgz::x");
}

TEST_F(MemberPathTest, AnAbsoluteContainerRendersWithAnEmptyUriAuthority) {
  // `archive://` + an absolute path = empty authority + path, exactly as `file:///...` works.
  EXPECT_THAT(JoinMemberPath("/abs/a.tar", "/inner/x", {.prefix = kUriPrefix}), "archive:///abs/a.tar!/inner/x");
}

TEST_F(MemberPathTest, ARelativeContainerRendersAsAnOpaqueUriNotAnAuthority) {
  // The bug this pins: `archive://a.tgz!x` would parse `a.tgz` as a HOST NAME, because `//`
  // introduces the authority. A relative container must therefore take the opaque form.
  EXPECT_THAT(JoinMemberPath("a.tgz", "inner/x", {.prefix = kUriPrefix}), "archive:a.tgz!inner/x");
  EXPECT_THAT(JoinMemberPath("sub/dir/a.tgz", "x", {.prefix = kUriPrefix}), "archive:sub/dir/a.tgz!x");
}

TEST_F(MemberPathTest, BothUriFormsParseBack) {
  const MemberPathOptions uri{.prefix = kUriPrefix};
  EXPECT_THAT(SplitMemberPath("archive:///abs/a.tar!/inner/x", uri), Optional(PartsAre("/abs/a.tar", "/inner/x")));
  EXPECT_THAT(SplitMemberPath("archive:a.tgz!inner/x", uri), Optional(PartsAre("a.tgz", "inner/x")));
}

TEST_F(MemberPathTest, SplitIsTheInverseOfJoin) {
  EXPECT_THAT(SplitMemberPath("a.tgz!inner/x"), Optional(PartsAre("a.tgz", "inner/x")));
  EXPECT_THAT(SplitMemberPath("a.tgz!/rooted"), Optional(PartsAre("a.tgz", "/rooted")));
  EXPECT_THAT(SplitMemberPath("a.tgz!//rooted", {.separator = "!/"}), Optional(PartsAre("a.tgz", "/rooted")));
}

TEST_F(MemberPathTest, SplitCutsAtTheFirstSeparatorAndKeepsTheRemainderVerbatim) {
  // A member whose own name contains the separator must survive intact; only the first one splits.
  EXPECT_THAT(SplitMemberPath("a.tgz!weird!name"), Optional(PartsAre("a.tgz", "weird!name")));
}

TEST_F(MemberPathTest, AnOrdinaryPathIsNotAMemberPath) {
  EXPECT_THAT(SplitMemberPath("plain/dir/file.txt"), Eq(std::nullopt));
  EXPECT_THAT(IsMemberPath("plain/dir/file.txt"), IsFalse());
  EXPECT_THAT(IsMemberPath("a.tgz!x"), IsTrue());
}

TEST_F(MemberPathTest, AnEmptySeparatorNeverMatches) {
  // Otherwise every path would split into an empty container plus itself.
  EXPECT_THAT(SplitMemberPath("a.tgz!x", {.separator = ""}), Eq(std::nullopt));
  EXPECT_THAT(IsMemberPath("anything", {.separator = ""}), IsFalse());
}

TEST_F(MemberPathTest, UriParsingRequiresTheSchemeItRenders) {
  EXPECT_THAT(SplitMemberPath("archive://a.tgz!x", {.prefix = kUriPrefix}), Optional(PartsAre("a.tgz", "x")));
  // Under --archive-prefix=uri a bare path is NOT a member path: the flag says what the spelling is,
  // so accepting both would make the two forms silently interchangeable.
  EXPECT_THAT(SplitMemberPath("a.tgz!x", {.prefix = kUriPrefix}), Eq(std::nullopt));
}

TEST_F(MemberPathTest, AnyOtherPrefixIsUsedLiterally) {
  // `prefix` is a string like `separator` is, so a system expecting its own marker can be fed one
  // with no code change. "URI" is the ONE keyword; nothing else is interpreted.
  EXPECT_THAT(JoinMemberPath("a.tgz", "x", {.prefix = "vfs:"}), "vfs:a.tgz!x");
  EXPECT_THAT(SplitMemberPath("vfs:a.tgz!x", {.prefix = "vfs:"}), Optional(PartsAre("a.tgz", "x")));
  // Strict both ways, for the same reason the URI form is: the flag says what the spelling IS.
  EXPECT_THAT(SplitMemberPath("a.tgz!x", {.prefix = "vfs:"}), Eq(std::nullopt));
}

TEST_F(MemberPathTest, AnEmptyPrefixMeansNoPrefixAndIsTheDefault) {
  // Empty, not the word "none": with a string-valued prefix, "none" would be indistinguishable from
  // a literal prefix spelled `none`.
  EXPECT_THAT(JoinMemberPath("a.tgz", "x", {.prefix = ""}), "a.tgz!x");
  EXPECT_THAT(JoinMemberPath("a.tgz", "x"), "a.tgz!x");
  EXPECT_THAT(SplitMemberPath("a.tgz!x", {.prefix = ""}), Optional(PartsAre("a.tgz", "x")));
  EXPECT_THAT(JoinMemberPath("none-dir/a.tgz", "x", {.prefix = "none"}), "nonenone-dir/a.tgz!x");
}

TEST_F(MemberPathTest, EverySeparatorSpellingRoundTrips) {
  // The property that matters in practice: a path xff printed parses back to the same two halves.
  const std::vector<std::string_view> separators = {"!", "#", "!/", "#/", "::", "/"};
  const std::vector<std::string_view> members = {"inner/x", "/rooted", "no-slash", "a b/c"};
  for (const std::string_view separator : separators) {
    for (const std::string_view member : members) {
      const MemberPathOptions options{.separator = separator};
      const std::string joined = JoinMemberPath("a.tgz", member, options);
      EXPECT_THAT(SplitMemberPath(joined, options), Optional(PartsAre("a.tgz", member)))
          << "separator='" << separator << "' member='" << member << "' joined='" << joined << "'";
    }
  }
}

TEST_F(MemberPathTest, AUriRoundTripsToo) {
  const MemberPathOptions options{.separator = "!/", .prefix = kUriPrefix};
  const std::string joined = JoinMemberPath("/abs/a.tar", "/inner/x", options);
  EXPECT_THAT(joined, "archive:///abs/a.tar!//inner/x");
  EXPECT_THAT(SplitMemberPath(joined, options), Optional(PartsAre("/abs/a.tar", "/inner/x")));
}

}  // namespace
}  // namespace xff::archive
