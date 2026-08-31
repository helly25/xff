// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
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

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Optional;

testing::Matcher<MemberPathParts> PartsAre(std::string_view container, std::string_view member) {
  return AllOf(
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
  // No "/" here: that separator needs filesystem context, and its own tests below cover it.
  const std::vector<std::string_view> separators = {"!", "#", "!/", "#/", "::"};
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

TEST_F(MemberPathTest, ASlashSeparatorNeedsFilesystemContextAndSaysSoInsteadOfGuessing) {
  const MemberPathOptions slash{.separator = "/"};
  // Rendering is fine - `/` is a legitimate spelling (PHP's phar:///a.phar/inner/x uses exactly it).
  EXPECT_THAT(JoinMemberPath("/abs/a.phar", "inner/x", slash), "/abs/a.phar/inner/x");
  // The string-only split REFUSES rather than cutting at the leading slash and reporting an empty
  // container, which is what a first-occurrence rule would do here.
  EXPECT_THAT(SplitMemberPath("/abs/a.phar/inner/x", slash), Eq(std::nullopt));
}

TEST_F(MemberPathTest, TheProbingSplitResolvesASlashSeparatorLikeAWalkDoes) {
  // The probe is what a traversal knows for free: walking down, `a.phar` IS a file it can open.
  const auto is_container = [](std::string_view path) { return path == "/abs/a.phar"; };
  const MemberPathOptions slash{.separator = "/"};
  EXPECT_THAT(
      SplitMemberPath("/abs/a.phar/inner/x", slash, is_container), Optional(PartsAre("/abs/a.phar", "inner/x")));
  // A path through no container is not a member path, however many separators it holds.
  EXPECT_THAT(SplitMemberPath("/abs/plain/dir/file", slash, is_container), Eq(std::nullopt));
}

TEST_F(MemberPathTest, TheOracleCanBePurelyLexicalSoSplittingNeedsNoFilesystem) {
  // How PHP resolves `phar:///path/a.phar/inner`: the EXTENSION is the split point, so `.phar/`
  // works with no stat at all and a printed path round-trips offline.
  const auto ends_in_phar = [](std::string_view path) { return path.ends_with(".phar"); };
  const MemberPathOptions slash{.separator = "/"};
  EXPECT_THAT(
      SplitMemberPath("/srv/app/a.phar/lib/x.php", slash, ends_in_phar),
      Optional(PartsAre("/srv/app/a.phar", "lib/x.php")));
  EXPECT_THAT(SplitMemberPath("/srv/app/plain/lib/x.php", slash, ends_in_phar), Eq(std::nullopt));
}

TEST_F(MemberPathTest, TheProbingSplitPicksTheOUTERMOSTContainer) {
  // Nested archives: left-to-right means the outer one wins, matching --archive-depth's outside-in
  // nesting. Choosing the innermost would silently reinterpret which archive is being addressed.
  const auto is_container = [](std::string_view path) { return path == "outer.tar" || path == "outer.tar/inner.zip"; };
  EXPECT_THAT(
      SplitMemberPath("outer.tar/inner.zip/x", {.separator = "/"}, is_container),
      Optional(PartsAre("outer.tar", "inner.zip/x")));
}

TEST_F(MemberPathTest, TheProbingSplitStillHonoursThePrefix) {
  const auto is_container = [](std::string_view path) { return path == "a.phar"; };
  const MemberPathOptions uri_slash{.separator = "/", .prefix = kUriPrefix};
  EXPECT_THAT(SplitMemberPath("archive:a.phar/inner", uri_slash, is_container), Optional(PartsAre("a.phar", "inner")));
  EXPECT_THAT(SplitMemberPath("a.phar/inner", uri_slash, is_container), Eq(std::nullopt));
}

TEST_F(MemberPathTest, AUriRoundTripsToo) {
  const MemberPathOptions options{.separator = "!/", .prefix = kUriPrefix};
  const std::string joined = JoinMemberPath("/abs/a.tar", "/inner/x", options);
  EXPECT_THAT(joined, "archive:///abs/a.tar!//inner/x");
  EXPECT_THAT(SplitMemberPath(joined, options), Optional(PartsAre("/abs/a.tar", "/inner/x")));
}

TEST_F(MemberPathTest, NormalizeMemberNameStripsTheSpellingsAContainerVaries) {
  // The three shapes one member can arrive in, from a tar / phar or from a user.
  EXPECT_THAT(NormalizeMemberName("dir/x"), "dir/x");
  EXPECT_THAT(NormalizeMemberName("./dir/x"), "dir/x");
  EXPECT_THAT(NormalizeMemberName(".././dir/x"), ".././dir/x");  // only a LEADING `./` is noise
  EXPECT_THAT(NormalizeMemberName("././dir/x"), "dir/x");
  EXPECT_THAT(NormalizeMemberName("dir/"), "dir");
  EXPECT_THAT(NormalizeMemberName("dir///"), "dir");
  EXPECT_THAT(NormalizeMemberName("./dir/"), "dir");
}

TEST_F(MemberPathTest, NormalizeMemberNameLeavesRootAndEmptyAlone) {
  // Reducing `/` to the empty name would turn "the root of the container" into "no member at all".
  EXPECT_THAT(NormalizeMemberName("/"), "/");
  EXPECT_THAT(NormalizeMemberName(""), "");
  EXPECT_THAT(NormalizeMemberName("/abs/x"), "/abs/x");
}

}  // namespace

TEST_F(MemberPathTest, APharRendersPhpsOwnUrl) {
  // PHP parses only `phar://`, with a plain `/` before the member - so a URI meant for PHP has to
  // be spelled PHP's way, whatever --archive-separator says for the bare path.
  EXPECT_THAT(JoinMemberPath("/abs/a.phar", "inner/x", {.prefix = kUriPrefix}), "phar:///abs/a.phar/inner/x");
  EXPECT_THAT(JoinMemberPath("a.phar", "inner/x", {.prefix = kUriPrefix}), "phar://a.phar/inner/x");
  // The separator flag does not reach this form.
  EXPECT_THAT(JoinMemberPath("/abs/a.phar", "x", {.separator = "#", .prefix = kUriPrefix}), "phar:///abs/a.phar/x");
}

TEST_F(MemberPathTest, AJarRendersJavasNestedUrl) {
  EXPECT_THAT(JoinMemberPath("/abs/a.jar", "pkg/C.class", {.prefix = kUriPrefix}), "jar:file:/abs/a.jar!/pkg/C.class");
  // war and ear are the same container kind under different names.
  EXPECT_THAT(
      JoinMemberPath("/abs/a.war", "WEB-INF/web.xml", {.prefix = kUriPrefix}), "jar:file:/abs/a.war!/WEB-INF/web.xml");
  EXPECT_THAT(JoinMemberPath("/abs/a.ear", "META-INF/x", {.prefix = kUriPrefix}), "jar:file:/abs/a.ear!/META-INF/x");
}

TEST_F(MemberPathTest, AnUnclaimedContainerKeepsTheGenericScheme) {
  // Only the extensions an ecosystem claims switch; `a.tar.gz` is a "gz", which claims nothing.
  EXPECT_THAT(JoinMemberPath("/abs/a.tar.gz", "x", {.prefix = kUriPrefix}), "archive:///abs/a.tar.gz!x");
  EXPECT_THAT(JoinMemberPath("/abs/a.zip", "x", {.prefix = kUriPrefix}), "archive:///abs/a.zip!x");
  // A dot in a DIRECTORY name is not an extension of the container.
  EXPECT_THAT(JoinMemberPath("/has.dots/plain", "x", {.prefix = kUriPrefix}), "archive:///has.dots/plain!x");
}

TEST_F(MemberPathTest, TheEcosystemFormsParseBackToo) {
  // Join and Split stay inverse for these forms as well, or a path xff printed would not round-trip.
  const MemberPathOptions uri{.prefix = kUriPrefix};
  EXPECT_THAT(SplitMemberPath("phar:///abs/a.phar/inner/x", uri), Optional(PartsAre("/abs/a.phar", "inner/x")));
  EXPECT_THAT(SplitMemberPath("phar://a.phar/inner/x", uri), Optional(PartsAre("a.phar", "inner/x")));
  EXPECT_THAT(
      SplitMemberPath("jar:file:/abs/a.jar!/pkg/C.class", uri), Optional(PartsAre("/abs/a.jar", "pkg/C.class")));
}

}  // namespace xff::archive
