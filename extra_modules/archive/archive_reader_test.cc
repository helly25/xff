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

#include <cstddef>
#include <cstdio>
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

TEST_F(ArchiveReaderTest, ListsMembersOfAnArchiveOnDisk) {
  // The streaming path is the one the walk will use (an archive is a file on disk, not bytes we
  // already hold), so it needs its own coverage: the memory path passing proves nothing about the
  // filename open.
  const std::string tar = MakeArchive({{.path = "on/disk.txt", .content = "content"}});
  const std::string path = absl::StrCat(::testing::TempDir(), "/xff_archive_reader_test.tar");
  {
    std::ofstream out(path, std::ios::binary);
    out.write(tar.data(), static_cast<std::streamsize>(tar.size()));
  }
  EXPECT_THAT(
      ListMembersOfFile(path),
      IsOkAndHolds(ElementsAre(AllOf(Field("path", &Member::path, "on/disk.txt"), Field("size", &Member::size, 7)))));
  std::remove(path.c_str());
}

TEST_F(ArchiveReaderTest, AMissingFileIsNotAnArchive) {
  // A path that cannot be opened must report the same "not an archive" status as unreadable data,
  // so the walk keeps treating it as an ordinary (unreadable) file rather than a corrupt archive.
  EXPECT_THAT(
      ListMembersOfFile(absl::StrCat(::testing::TempDir(), "/xff_archive_no_such_file.tar")),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveReaderTest, PlainDataIsNotAnArchive) {
  // "Not an archive" must stay distinguishable from "broken archive": the walk treats the first as
  // an ordinary file and only reports the second.
  EXPECT_THAT(ListMembers("just some text, definitely not an archive"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveReaderTest, EmptyInputIsNotAnArchive) {
  EXPECT_THAT(ListMembers(""), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveReaderTest, RegistersItsLicenseNotice) {
  // Linking the extra must contribute libarchive's notice, so --help=notice / the NOTICE file stay
  // complete by construction rather than by anyone remembering to update them.
  EXPECT_THAT(license::Notices(), Contains(Field("component", &license::Notice::component, "libarchive")));
}

}  // namespace
}  // namespace xff::archive
