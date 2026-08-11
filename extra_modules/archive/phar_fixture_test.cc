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

// Reads the committed phar fixtures - containers written by PHP itself (see test_data/README.md).
//
// phar_reader_test builds containers byte by byte, which pins what we BELIEVE the format is. This
// test pins that the belief matches the reference implementation, across every packaging variant a
// phar can arrive in: native stored, native per-member compressed, native whole-file compressed,
// signed, and the tar- / zip-based variants (which are ordinary archives and go through the
// libarchive reader instead). Where a variant is not supported yet, the case pins the ERROR, so the
// gap is visible here rather than asserted in prose.

#include <cstdlib>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_reader.h"
#include "xff/archive/phar_reader.h"

namespace xff::archive {
namespace {

using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Field;
using ::testing::Ge;
using ::testing::HasSubstr;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::NotNull;

// The member set every fixture carries, from tools/make_phar_fixtures.php. Sizes are the
// UNCOMPRESSED lengths, so they hold for the compressed variants too.
constexpr std::string_view kRunPhp = "bin/run.php";
constexpr std::string_view kUtilPhp = "lib/util.php";
constexpr std::string_view kReadme = "data/readme.txt";
constexpr std::string_view kReadmeContent = "This is the xff phar fixture.\nfindable-needle\n";

struct PharFixtureTest : ::testing::Test {
  // The fixture directory, from the `data` dep. Bazel hands the test one fixture's runfiles path in
  // the environment (see the BUILD file) because this test lives in an EXTERNAL module: the runfiles
  // layout there is under the module's canonical repo directory, whose name (`xff_archive+`) is a
  // bazel implementation detail no test should hard-code.
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
};

TEST_F(PharFixtureTest, TheNativeFixturesAreStillByteIntactContainers) {
  // Not paranoia: an end-of-file "fixer" appended a newline to three of these containers, and every
  // functional test below still passed - a byte at EOF moves neither the manifest nor any member's
  // data. PHP, however, rejects the result: the signature covers the whole file and its `GBMB` magic
  // must be the LAST four bytes. So the fixtures had quietly stopped being what they claim to be, with
  // nothing to say so.
  //
  // Only the uncompressed native fixtures are checked. In the whole-file compressed and the tar / zip
  // variants the trailer sits inside the compressed stream or behind the format's own end record, and
  // those files are binary from byte 0 anyway - it is precisely a container that STARTS with text
  // (`<?php`) that a text tool mistakes for editable.
  for (const std::string_view fixture : {
           std::string_view("plain.phar"),
           std::string_view("sha256.phar"),
           std::string_view("entrygz.phar"),
           std::string_view("entrybz2.phar"),
       }) {
    std::ifstream file(Fixture(fixture), std::ios::binary);
    ASSERT_THAT(file.is_open(), IsTrue()) << fixture;
    std::string trailer(4, '\0');
    file.seekg(-4, std::ios::end);
    file.read(trailer.data(), 4);
    EXPECT_THAT(trailer, "GBMB") << fixture << ": signature trailer is not at the end of the file";
  }
}

TEST_F(PharFixtureTest, ANativePharFromPhpListsTheMembersWeExpect) {
  // The one case that proves the format was read correctly rather than consistently: these bytes
  // came out of PHP, and every field offset in the manifest had to be right to get here.
  const auto members = ListPharMembersOfFile(Fixture("plain.phar"));
  ASSERT_THAT(members, IsOkAndHolds(Not(::testing::IsEmpty())));
  EXPECT_THAT(
      *members, Contains(AllOf(
                    Field("path", &Member::path, kRunPhp), Field("size", &Member::size, Ge(1)),
                    Field("is_directory", &Member::is_directory, ::testing::IsFalse()),
                    // PHP stamps the current time, so only the shape is assertable.
                    Field("mtime", &Member::mtime, Ge(1'600'000'000)))));
  EXPECT_THAT(*members, Contains(Field("path", &Member::path, kUtilPhp)));
  EXPECT_THAT(*members, Contains(Field("path", &Member::path, kReadme)));
}

TEST_F(PharFixtureTest, AnExplicitEmptyDirectoryIsADirectoryMember) {
  // PHP writes `addEmptyDir('var/empty')` as a name with a trailing slash and no data. If the
  // trailing-slash convention were misread, this would show up as a zero-byte FILE.
  const auto members = ListPharMembersOfFile(Fixture("plain.phar"));
  ASSERT_THAT(members, IsOkAndHolds(Not(::testing::IsEmpty())));
  EXPECT_THAT(
      *members,
      Contains(
          AllOf(Field("path", &Member::path, "var/empty"), Field("is_directory", &Member::is_directory, IsTrue()))));
}

TEST_F(PharFixtureTest, AStoredMemberReadsBackByteForByte) {
  EXPECT_THAT(ReadPharMemberOfFile(Fixture("plain.phar"), kReadme), IsOkAndHolds(kReadmeContent));
}

TEST_F(PharFixtureTest, ASignedPharIsReadTheSameWay) {
  // A signature appends bytes AFTER the member data (signature + type + the `GBMB` magic). Nothing
  // in the manifest announces them, so a reader that computed a member's extent from "the rest of
  // the file" rather than from its stored size would read the signature as content.
  EXPECT_THAT(ReadPharMemberOfFile(Fixture("sha256.phar"), kReadme), IsOkAndHolds(kReadmeContent));
  EXPECT_THAT(ListPharMembersOfFile(Fixture("sha256.phar")), IsOkAndHolds(Not(::testing::IsEmpty())));
}

TEST_F(PharFixtureTest, PerMemberCompressionListsButDoesNotReadYet) {
  // The manifest is plain in these, so listing is fully supported and reports the UNCOMPRESSED size.
  // Reading is refused rather than returning deflate / bzip2 bytes as if they were content.
  for (const std::string_view fixture : {std::string_view("entrygz.phar"), std::string_view("entrybz2.phar")}) {
    const std::string path = Fixture(fixture);
    EXPECT_THAT(ListPharMembersOfFile(path), IsOkAndHolds(Contains(Field("path", &Member::path, kReadme)))) << fixture;
    EXPECT_THAT(ReadPharMemberOfFile(path, kReadme), StatusIs(absl::StatusCode::kUnimplemented)) << fixture;
  }
}

TEST_F(PharFixtureTest, AWholeFileCompressedPharIsNotYetRecognized) {
  // `Phar::compress()` wraps the ENTIRE container, so the stub - and with it the halt token - is
  // inside the compressed stream. Until the reader decompresses the container first, this is
  // correctly reported as "not a phar" (the walk then treats it as an ordinary file) rather than as
  // a corrupt one.
  for (const std::string_view fixture : {std::string_view("wholegz.phar.gz"), std::string_view("wholebz2.phar.bz2")}) {
    EXPECT_THAT(
        ListPharMembersOfFile(Fixture(fixture)),
        StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("__HALT_COMPILER")))
        << fixture;
  }
}

TEST_F(PharFixtureTest, TheTarAndZipBasedVariantsNeedNoPharCodeAtAll) {
  // These are ordinary archives, which is exactly the point: the libarchive reader handles them, so
  // `.phar.tar` / `.phar.tgz` / `.phar.zip` work without the phar reader being involved. PHP stores
  // its own bookkeeping as members (`.phar/stub.php`, `.phar/signature.bin`), which is why the
  // assertion is Contains rather than an exact list.
  for (const std::string_view fixture : {
           std::string_view("tarbased.phar.tar"),
           std::string_view("targz.phar.tar.gz"),
           std::string_view("zipbased.phar.zip"),
       }) {
    const std::string path = Fixture(fixture);
    EXPECT_THAT(ListMembersOfFile(path), IsOkAndHolds(Contains(Field("path", &Member::path, kReadme)))) << fixture;
    EXPECT_THAT(ReadMemberOfFile(path, kReadme), IsOkAndHolds(kReadmeContent)) << fixture;
  }
}

TEST_F(PharFixtureTest, TheNativeVariantsAreNotArchivesToLibarchive) {
  // The converse, and the reason the phar reader exists: libarchive does not recognise the native
  // format, so it must report "not an archive" rather than half-opening it.
  EXPECT_THAT(ListMembersOfFile(Fixture("plain.phar")), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(PharFixtureTest, ATarBasedPharIsNotANativeOne) {
  // And the phar reader must not claim the tar-based variant: the two readers partition the space
  // instead of both guessing at it.
  EXPECT_THAT(ListPharMembersOfFile(Fixture("tarbased.phar.tar")), StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace xff::archive
