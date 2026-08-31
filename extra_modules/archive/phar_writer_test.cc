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
//
// Removes members from the committed phar fixtures - real containers written by PHP - and reads the
// result back. Working on the genuine article matters here more than anywhere: a hand-built fixture
// would only prove the writer agrees with our own reader, while these prove it agrees with PHP's
// layout, signature trailer included.
//
// Each case copies its fixture to a temporary file first: a test that rewrote the committed fixture
// would corrupt the input of every other test in the module (and of the next run).

#include "xff/archive/phar_writer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_reader.h"
#include "xff/archive/phar_reader.h"

namespace xff::archive {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::SizeIs;

constexpr std::string_view kRunPhp = "bin/run.php";
constexpr std::string_view kUtilPhp = "lib/util.php";
constexpr std::string_view kReadme = "data/readme.txt";
constexpr std::string_view kReadmeContent = "This is the xff phar fixture.\nfindable-needle\n";

struct PharWriterTest : ::testing::Test {
  // The fixture directory, from the `data` dep, located the same way phar_fixture_test does it (this
  // module is external, so its runfiles path carries a canonical repo name no test should hard-code).
  static std::string Fixture(std::string_view name) {
    // Bazel's own environment, read once in a single-threaded test; getenv is safe here.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char* const anchor = std::getenv("XFF_PHAR_FIXTURE_ANCHOR");
    EXPECT_THAT(anchor, NotNull()) << "the BUILD file must set XFF_PHAR_FIXTURE_ANCHOR";
    const std::string_view anchor_path = anchor != nullptr ? anchor : "";
    const std::string_view::size_type slash = anchor_path.rfind('/');
    const std::string_view directory = slash == std::string_view::npos ? "." : anchor_path.substr(0, slash);
    return absl::StrCat(directory, "/", name);
  }

  static std::string Read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()};
  }

  // A writable copy of a fixture, so the committed one is never touched.
  static std::string CopyOfFixture(std::string_view fixture, std::string_view as) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char* const tmp = std::getenv("TEST_TMPDIR");
    const std::string path = absl::StrCat(tmp != nullptr ? tmp : "/tmp", "/", as);
    const std::string bytes = Read(Fixture(fixture));
    EXPECT_THAT(bytes, Not(IsEmpty())) << fixture;
    std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return path;
  }

  static std::vector<std::string> MemberNames(const std::string& path) {
    std::vector<std::string> names;
    const absl::StatusOr<std::vector<Member>> members = ListPharMembersOfFile(path);
    EXPECT_THAT(members, IsOk());
    if (members.ok()) {
      for (const Member& member : *members) {
        names.push_back(member.path);
      }
    }
    return names;
  }

  static std::string CopyWithSignature(std::string_view as, std::size_t digest_size, std::uint32_t type) {
    std::string bytes = Read(Fixture("plain.phar"));
    bytes.resize(bytes.size() - 40);  // SHA-256 digest, type word, and GBMB trailer.
    bytes.append(digest_size, '\0');
    for (std::size_t i = 0; i < 4; ++i) {
      bytes.push_back(static_cast<char>((type >> (8U * i)) & 0xffU));
    }
    bytes.append("GBMB");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char* const tmp = std::getenv("TEST_TMPDIR");
    const std::string path = absl::StrCat(tmp != nullptr ? tmp : "/tmp", "/", as);
    std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return path;
  }
};

TEST_F(PharWriterTest, RemovingAMemberLeavesTheOtherMembersReadable) {
  // The whole contract on a real PHP-written container: the named member is gone, the others are
  // still listed AND still hold their own bytes (the manifest entries and stored data that follow a
  // removal all shift, so a rewrite that miscounted would corrupt them).
  const std::string path = CopyOfFixture("plain.phar", "removed.phar");
  ASSERT_THAT(MemberNames(path), Contains(std::string(kUtilPhp)));
  EXPECT_THAT(RemovePharMembersOfFile(path, {std::string(kUtilPhp)}), IsOk());
  EXPECT_THAT(MemberNames(path), Not(Contains(std::string(kUtilPhp))));
  EXPECT_THAT(MemberNames(path), Contains(std::string(kRunPhp)));
  EXPECT_THAT(ReadPharMemberOfFile(path, kReadme), IsOkAndHolds(std::string(kReadmeContent)));
}

TEST_F(PharWriterTest, TheSignatureTrailerIsRecomputedAndStaysLast) {
  // A phar's signature is a digest over everything before it, so it cannot be copied - and its `GBMB`
  // magic has to remain the last four bytes or PHP rejects the container outright.
  const std::string path = CopyOfFixture("sha256.phar", "resigned.phar");
  const std::string before = Read(path);
  EXPECT_THAT(RemovePharMembersOfFile(path, {std::string(kUtilPhp)}), IsOk());
  const std::string after = Read(path);
  EXPECT_THAT(after.substr(after.size() - 4), "GBMB");
  // The type word is untouched (sha256 stays sha256) while the digest itself changed.
  EXPECT_THAT(after.substr(after.size() - 8, 4), before.substr(before.size() - 8, 4));
  EXPECT_THAT(after.substr(after.size() - 40, 32), Not(before.substr(before.size() - 40, 32)));
}

TEST_F(PharWriterTest, EveryDigestSignatureCanBeRecomputed) {
  struct SignatureSpec {
    std::string_view name;
    std::size_t digest_size;
    std::uint32_t type;
  };

  constexpr std::array kSignatures = std::to_array<SignatureSpec>({
      {.name = "md5", .digest_size = 16, .type = 1},
      {.name = "sha1", .digest_size = 20, .type = 2},
      {.name = "sha512", .digest_size = 64, .type = 4},
  });
  for (const SignatureSpec& signature : kSignatures) {
    const std::string path =
        CopyWithSignature(absl::StrCat(signature.name, ".phar"), signature.digest_size, signature.type);
    EXPECT_THAT(RemovePharMembersOfFile(path, {std::string(kUtilPhp)}), IsOk()) << signature.name;
    EXPECT_THAT(ReadPharMemberOfFile(path, kReadme), IsOkAndHolds(std::string(kReadmeContent))) << signature.name;
  }
}

TEST_F(PharWriterTest, OpenSslSignaturesCannotBeRecomputedWithoutTheKey) {
  constexpr std::uint32_t kOpenSslSignature = 0x10;
  const std::string path = CopyWithSignature("openssl.phar", /*digest_size=*/16, kOpenSslSignature);
  EXPECT_THAT(
      RemovePharMembersOfFile(path, {std::string(kUtilPhp)}),
      StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("openssl or unknown")));
}

TEST_F(PharWriterTest, APerMemberCompressedMemberSurvivesUntouched) {
  // Each member carries its own compression, so a removal must copy the survivors' STORED bytes
  // rather than re-encode them - otherwise a gz member would come back as garbage.
  const std::string path = CopyOfFixture("entrygz.phar", "gzsurvivor.phar");
  EXPECT_THAT(RemovePharMembersOfFile(path, {std::string(kRunPhp)}), IsOk());
  EXPECT_THAT(MemberNames(path), Not(Contains(std::string(kRunPhp))));
  EXPECT_THAT(ReadPharMemberOfFile(path, kReadme), IsOkAndHolds(std::string(kReadmeContent)));
}

TEST_F(PharWriterTest, RemovingEveryMemberLeavesAnEmptyButValidPhar) {
  // An archive with no members is legal, and it is not xff's business to delete the FILE when the
  // request was to remove members from it.
  const std::string path = CopyOfFixture("plain.phar", "emptied.phar");
  std::vector<std::string> all = MemberNames(path);
  std::erase(all, ".phar/stub.php");
  ASSERT_THAT(all, Not(IsEmpty()));
  EXPECT_THAT(RemovePharMembersOfFile(path, all), IsOk());
  EXPECT_THAT(MemberNames(path), ElementsAre(".phar/stub.php"));
  const std::string after = Read(path);
  EXPECT_THAT(after.substr(after.size() - 4), "GBMB");
}

TEST_F(PharWriterTest, SyntheticNativeStubCannotBeRemoved) {
  const std::string path = CopyOfFixture("plain.phar", "keep-stub.phar");
  const std::string before = Read(path);
  EXPECT_THAT(
      RemovePharMembersOfFile(path, {".phar/stub.php"}),
      StatusIs(absl::StatusCode::kFailedPrecondition, HasSubstr("requires its executable stub")));
  EXPECT_THAT(Read(path), before);
}

TEST_F(PharWriterTest, AMemberThatIsNotThereIsNotFoundAndChangesNothing) {
  const std::string path = CopyOfFixture("plain.phar", "untouched.phar");
  const std::string before = Read(path);
  EXPECT_THAT(
      RemovePharMembersOfFile(path, {std::string(kRunPhp), "nope.php"}),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("nope.php")));
  EXPECT_THAT(Read(path), before);  // byte for byte, not merely still a valid phar
}

TEST_F(PharWriterTest, APlainFileIsNotAPhar) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* const tmp = std::getenv("TEST_TMPDIR");
  const std::string path = absl::StrCat(tmp != nullptr ? tmp : "/tmp", "/notes.txt");
  std::ofstream(path, std::ios::binary) << "just text\n";
  EXPECT_THAT(RemovePharMembersOfFile(path, {"one"}), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(PharWriterTest, RemovingNothingIsNotAWrite) {
  const std::string path = CopyOfFixture("plain.phar", "noop.phar");
  const std::string before = Read(path);
  EXPECT_THAT(RemovePharMembersOfFile(path, {}), IsOk());
  EXPECT_THAT(Read(path), before);
}

TEST_F(PharWriterTest, ATarOrZipBasedPharIsRecognisedByItsSignatureMember) {
  // Those variants are ordinary tars / zips, so the libarchive writer WOULD rewrite them - and the
  // result is a container PHP rejects, because their signature is a member computed over everything
  // else. This is the check that stops it; a container with no signature member is not flagged.
  MBO_ASSERT_OK_AND_ASSIGN(const std::vector<Member> tar_based, ListMembersOfFile(Fixture("tarbased.phar.tar")));
  EXPECT_THAT(IsSignedTarOrZipPhar(tar_based), IsTrue());
  MBO_ASSERT_OK_AND_ASSIGN(const std::vector<Member> zip_based, ListMembersOfFile(Fixture("zipbased.phar.zip")));
  EXPECT_THAT(IsSignedTarOrZipPhar(zip_based), IsTrue());
  EXPECT_THAT(IsSignedTarOrZipPhar({Member{.path = "bin/run.php"}, Member{.path = ".phar/stub.php"}}), IsFalse());
}

TEST_F(PharWriterTest, TheLayoutMatchesTheFileItWasReadFrom) {
  // The writer works from PharLayout, so its own invariants are worth pinning: the manifest sits
  // after the stub, the data section after the manifest, and the members' bytes end where the
  // signature begins.
  const std::string bytes = Read(Fixture("plain.phar"));
  MBO_ASSERT_OK_AND_ASSIGN(const PharLayout layout, ParsePharLayout(bytes));
  std::vector<std::string> listed = MemberNames(Fixture("plain.phar"));
  std::erase(listed, ".phar/stub.php");  // synthetic, so deliberately absent from the manifest layout
  EXPECT_THAT(layout.members, SizeIs(listed.size()));
  EXPECT_THAT(layout.manifest_start > layout.manifest_length_at, IsTrue());
  EXPECT_THAT(layout.data_offset, layout.manifest_start + layout.manifest_size);
  EXPECT_THAT(layout.entries_offset > layout.manifest_start, IsTrue());
  EXPECT_THAT(layout.data_end < bytes.size(), IsTrue());  // the signature trailer follows
}

}  // namespace
}  // namespace xff::archive
