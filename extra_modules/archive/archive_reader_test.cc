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

#include "xff/archive/archive_reader.h"

#include <archive.h>
#include <archive_entry.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/license/notice.h"

namespace xff::archive {
namespace {

using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsTrue;
using ::testing::SizeIs;

// One file to place into a generated archive.
struct FileSpec {
  std::string path;
  std::string content;
};

struct ArchiveReaderTest : ::testing::Test {
  // Builds an archive in memory with libarchive's WRITE API, so the test needs no committed binary
  // fixture and exercises the same library end to end. `format` is a libarchive write-format setter
  // ("pax_restricted" tar here); `gzip` adds the gz filter, proving filter detection on read.
  static std::string MakeArchive(const std::vector<FileSpec>& files, bool gzip = false) {
    struct ::archive* out = ::archive_write_new();
    EXPECT_THAT(out != nullptr, IsTrue());
    ::archive_write_set_format_pax_restricted(out);
    if (gzip) {
      ::archive_write_add_filter_gzip(out);
    }
    std::string buffer(64 * 1'024, '\0');
    std::size_t used = 0;
    ::archive_write_open_memory(out, buffer.data(), buffer.size(), &used);
    for (const FileSpec& file : files) {
      struct ::archive_entry* entry = ::archive_entry_new();
      ::archive_entry_set_pathname(entry, file.path.c_str());
      ::archive_entry_set_size(entry, static_cast<std::int64_t>(file.content.size()));
      ::archive_entry_set_filetype(entry, AE_IFREG);
      ::archive_entry_set_perm(entry, 0644);
      ::archive_write_header(out, entry);
      ::archive_write_data(out, file.content.data(), file.content.size());
      ::archive_entry_free(entry);
    }
    ::archive_write_close(out);
    ::archive_write_free(out);
    buffer.resize(used);
    return buffer;
  }

  // The same bytes on disk: ReadMemberOfFile / ListMembersOfFile stream from a path, so a file is
  // needed - built from MakeArchive so both entry points see byte-identical input.
  static std::string WriteArchive(const std::vector<FileSpec>& files, std::string_view name) {
    const char* const tmp = std::getenv("TEST_TMPDIR");
    const std::string path = absl::StrCat(tmp != nullptr ? tmp : "/tmp", "/", name);
    const std::string bytes = MakeArchive(files);
    std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return path;
  }

  // A tar carrying an explicit DIRECTORY member, for the "no content to read" case.
  static std::string WriteArchiveWithDirectory(std::string_view name) {
    const char* const tmp = std::getenv("TEST_TMPDIR");
    const std::string path = absl::StrCat(tmp != nullptr ? tmp : "/tmp", "/", name);
    struct ::archive* out = ::archive_write_new();
    ::archive_write_set_format_pax_restricted(out);
    ::archive_write_open_filename(out, path.c_str());
    struct ::archive_entry* dir = ::archive_entry_new();
    ::archive_entry_set_pathname(dir, "dir");
    ::archive_entry_set_filetype(dir, AE_IFDIR);
    ::archive_entry_set_perm(dir, 0755);
    ::archive_write_header(out, dir);
    ::archive_entry_free(dir);
    ::archive_write_close(out);
    ::archive_write_free(out);
    return path;
  }
};

TEST_F(ArchiveReaderTest, ListsTarMembersWithTheirPathsAndSizes) {
  const std::string tar = MakeArchive({{.path = "dir/a.txt", .content = "abc"}, {.path = "b.bin", .content = "xyzw"}});
  EXPECT_THAT(
      ListMembers(tar), IsOkAndHolds(ElementsAre(
                            AllOf(Field("path", &Member::path, "dir/a.txt"), Field("size", &Member::size, 3)),
                            AllOf(Field("path", &Member::path, "b.bin"), Field("size", &Member::size, 4)))));
}

TEST_F(ArchiveReaderTest, DetectsTheCompressionFilterFromContent) {
  // A gzip-filtered tar reads identically: the caller never names the format or the filter, which
  // is what lets one --archive flag cover tar / tgz / zip / ... uniformly.
  const std::string tgz = MakeArchive({{.path = "only.txt", .content = "hello"}}, /*gzip=*/true);
  EXPECT_THAT(
      ListMembers(tgz),
      IsOkAndHolds(ElementsAre(AllOf(Field("path", &Member::path, "only.txt"), Field("size", &Member::size, 5)))));
}

TEST_F(ArchiveReaderTest, ReportsMemberMetadata) {
  const std::string tar = MakeArchive({{.path = "m.txt", .content = "data"}});
  const auto members = ListMembers(tar);
  ASSERT_THAT(members, IsOkAndHolds(SizeIs(1)));
  EXPECT_THAT(members->front().mode, 0644);
  EXPECT_THAT(members->front().is_directory, ::testing::IsFalse());
  EXPECT_THAT(members->front().is_symlink, ::testing::IsFalse());
}

TEST_F(ArchiveReaderTest, PlainDataIsNotAnArchive) {
  // "Not an archive" must stay distinguishable from "broken archive": the walk treats the first as
  // an ordinary file and only reports the second.
  EXPECT_THAT(ListMembers("just some text, definitely not an archive"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveReaderTest, EmptyInputIsNotAnArchive) {
  EXPECT_THAT(ListMembers(""), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveReaderTest, TextThatMtreeWouldClaimIsStillNotAnArchive) {
  // mtree is a plain-text format with no magic, so `archive_read_support_format_all` accepts
  // ordinary text as an "archive" whose members are named after its words. That made `xff notes.txt`
  // (the xff family dives a named root by default) invent a member, so the reader enables its
  // formats explicitly and leaves mtree out. Each line below is text a walk really meets.
  constexpr std::array kTextThatIsNotAnArchive = std::to_array<std::string_view>({
      "needle\n", "hello world\n", "./relative/path/looking/line\n",
      "#mtree\n./notes type=file\n",  // even the real mtree preamble stays a text file to us
  });
  for (const std::string_view text : kTextThatIsNotAnArchive) {
    EXPECT_THAT(ListMembers(text), StatusIs(absl::StatusCode::kInvalidArgument)) << text;
  }
}

TEST_F(ArchiveReaderTest, RegistersItsLicenseNotice) {
  // Linking the extra must contribute libarchive's notice, so --help=notice / the NOTICE file stay
  // complete by construction rather than by anyone remembering to update them.
  EXPECT_THAT(license::Notices(), Contains(Field("component", &license::Notice::component, "libarchive")));
}

// ReadMemberOfFile: the entry point the content predicates need. Each error state is distinct on
// purpose, so a caller can tell "no such member" from "member has no content" from "bomb guard".
TEST_F(ArchiveReaderTest, ReadMemberOfFileReturnsTheMemberContent) {
  const std::string tar = WriteArchive({{.path = "hello.txt", .content = "hello\n"}}, "read.tar");
  EXPECT_THAT(ReadMemberOfFile(tar, "hello.txt"), IsOkAndHolds("hello\n"));
}

TEST_F(ArchiveReaderTest, ReadMemberOfFileIgnoresALeadingDotSlashOnEitherSide) {
  // Tar streams write both spellings for the same member, so neither side may be authoritative.
  const std::string tar = WriteArchive({{.path = "hello.txt", .content = "hello\n"}}, "dotslash.tar");
  EXPECT_THAT(ReadMemberOfFile(tar, "./hello.txt"), IsOkAndHolds("hello\n"));
}

TEST_F(ArchiveReaderTest, ReadMemberOfFileReportsAMissingMemberAsNotFound) {
  const std::string tar = WriteArchive({{.path = "hello.txt", .content = "hello\n"}}, "missing.tar");
  EXPECT_THAT(ReadMemberOfFile(tar, "nope.txt"), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(ArchiveReaderTest, ReadMemberOfFileRefusesSomethingWithNoContent) {
  // A directory member: FailedPrecondition, not an empty string - a content predicate could not
  // distinguish an empty string here from a genuinely empty file.
  const std::string tar = WriteArchiveWithDirectory("dir.tar");
  EXPECT_THAT(ReadMemberOfFile(tar, "dir"), StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(ArchiveReaderTest, ReadMemberOfFileRejectsANonArchive) {
  EXPECT_THAT(ReadMemberOfFile("/etc/hosts", "anything"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveReaderTest, ReadMemberOfFileEnforcesTheByteLimit) {
  // The bomb guard: a small header can promise a huge expansion, so the LOOP bounds the read rather
  // than trusting the declared size.
  const std::string tar = WriteArchive({{.path = "hello.txt", .content = "hello\n"}}, "limit.tar");
  EXPECT_THAT(ReadMemberOfFile(tar, "hello.txt", /*max_bytes=*/2), StatusIs(absl::StatusCode::kResourceExhausted));
  // The limit is inclusive: content exactly at the limit is fine.
  EXPECT_THAT(ReadMemberOfFile(tar, "hello.txt", /*max_bytes=*/6), IsOkAndHolds("hello\n"));
}

}  // namespace
}  // namespace xff::archive
