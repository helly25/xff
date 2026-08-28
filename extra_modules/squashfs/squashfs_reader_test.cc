// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/squashfs/squashfs_reader.h"

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status_macros.h"
#include "mbo/testing/status.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::squashfs {
namespace {

using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::NotNull;

struct SquashfsReaderTest : ::testing::Test {
  static std::string Fixture() {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): Bazel sets the immutable test environment before launch.
    const char* const path = std::getenv("XFF_SQUASHFS_FIXTURE");
    EXPECT_THAT(path, NotNull());
    return path == nullptr ? std::string() : path;
  }

  static std::string FixtureBytes() {
    std::ifstream input(Fixture(), std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }

  static auto IsEntry(std::string_view name, vfs::FileType type) {
    return AllOf(Field("name", &vfs::Entry::name, name), Field("type", &vfs::Entry::type, type));
  }
};

TEST_F(SquashfsReaderTest, ExposesTheStoredTreeMetadataAndSymlink) {
  MBO_ASSERT_OK_AND_ASSIGN(const SquashfsFileSystem fs, SquashfsFileSystem::Open(Fixture()));
  EXPECT_THAT(
      fs.ReadDir(Fixture()),
      IsOkAndHolds(ElementsAre(
          IsEntry("dir", vfs::FileType::kDirectory), IsEntry("hello-link", vfs::FileType::kSymlink),
          IsEntry("hello.txt", vfs::FileType::kRegular))));
  EXPECT_THAT(
      fs.ReadDir(Fixture() + "!dir/sub"), IsOkAndHolds(ElementsAre(IsEntry("data.bin", vfs::FileType::kRegular))));
  EXPECT_THAT(
      fs.Stat(Fixture() + "!hello.txt", /*follow_symlinks=*/false),
      IsOkAndHolds(AllOf(
          Field("size", &vfs::Metadata::size, 20), Field("mode", &vfs::Metadata::mode, 0640),
          Field("source", &vfs::Metadata::source, vfs::Source::kArchiveMember))));
  EXPECT_THAT(fs.ReadLink(Fixture() + "!hello-link"), IsOkAndHolds("hello.txt"));
}

TEST_F(SquashfsReaderTest, PathAndRetainedByteImagesExposeTheSameContent) {
  MBO_ASSERT_OK_AND_ASSIGN(const SquashfsFileSystem path_fs, SquashfsFileSystem::Open(Fixture()));
  MBO_ASSERT_OK_AND_ASSIGN(
      const SquashfsFileSystem memory_fs, SquashfsFileSystem::OpenBytes("memory.sqfs", FixtureBytes()));
  EXPECT_THAT(path_fs.ReadContent(Fixture() + "!hello.txt"), IsOkAndHolds("hello from squashfs\n"));
  EXPECT_THAT(
      memory_fs.ReadContent("memory.sqfs!/dir/sub/data.bin"), IsOkAndHolds(Eq(std::string("\x00\x01\x02\x03\xff", 5))));
}

TEST_F(SquashfsReaderTest, ReadsAnAppImageStylePrefixedPayloadFromMemoryAndDisk) {
  // An executable may itself contain the four magic bytes. A candidate is accepted only when
  // libsqsh validates the superblock, so the earlier incidental string cannot hide the payload.
  const std::string prefixed = std::string("ELF hsqs-style executable prefix\0with data", 42) + FixtureBytes();
  MBO_ASSERT_OK_AND_ASSIGN(
      const SquashfsFileSystem memory_fs, SquashfsFileSystem::OpenBytes("program.AppImage", prefixed));
  EXPECT_THAT(memory_fs.ReadContent("program.AppImage!hello.txt"), IsOkAndHolds("hello from squashfs\n"));

  const std::string path = ::testing::TempDir() + "/fixture.appimage";
  std::ofstream(path, std::ios::binary).write(prefixed.data(), static_cast<std::streamsize>(prefixed.size()));
  MBO_ASSERT_OK_AND_ASSIGN(const SquashfsFileSystem path_fs, SquashfsFileSystem::Open(path));
  EXPECT_THAT(path_fs.ReadContent(path + "!hello.txt"), IsOkAndHolds("hello from squashfs\n"));
}

TEST_F(SquashfsReaderTest, FindsAnAppImagePayloadAcrossItsScanBlockBoundary) {
  constexpr std::size_t kScanBlockSize = std::size_t{64} * 1'024;
  const std::string prefixed = std::string(kScanBlockSize - 2, '\0') + FixtureBytes();
  const std::string path = ::testing::TempDir() + "/boundary.appimage";
  std::ofstream(path, std::ios::binary).write(prefixed.data(), static_cast<std::streamsize>(prefixed.size()));
  MBO_ASSERT_OK_AND_ASSIGN(const SquashfsFileSystem fs, SquashfsFileSystem::Open(path));
  EXPECT_THAT(fs.ReadContent(path + "!hello.txt"), IsOkAndHolds("hello from squashfs\n"));
}

TEST_F(SquashfsReaderTest, RejectsNonImagesAndTruncatedImages) {
  EXPECT_THAT(
      SquashfsFileSystem::OpenBytes("plain.sqfs", "not squashfs"), StatusIs(absl::StatusCode::kInvalidArgument));
  const std::string bytes = FixtureBytes();
  EXPECT_THAT(
      SquashfsFileSystem::OpenBytes("truncated.sqfs", bytes.substr(0, 96)), StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(
      SquashfsFileSystem::OpenBytes("plain.appimage", "ELF without a SquashFS payload"),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("cannot find")));
  EXPECT_THAT(SquashfsFileSystem::Open("/definitely/not/a/squashfs/image.sqfs"), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(SquashfsReaderTest, ReadOnlyFilesystemRejectsInvalidOperationsPrecisely) {
  MBO_ASSERT_OK_AND_ASSIGN(const SquashfsFileSystem fs, SquashfsFileSystem::Open(Fixture()));
  EXPECT_THAT(fs.Access(Fixture() + "!hello.txt", vfs::AccessMode::kRead), IsTrue());
  EXPECT_THAT(fs.Access(Fixture() + "!hello.txt", vfs::AccessMode::kWrite), IsFalse());
  EXPECT_THAT(fs.Access(Fixture() + "!hello.txt", vfs::AccessMode::kExecute), IsFalse());
  EXPECT_THAT(fs.Access(Fixture() + "!dir", vfs::AccessMode::kExecute), IsTrue());
  EXPECT_THAT(fs.Access(Fixture() + "!missing", vfs::AccessMode::kRead), IsFalse());
  EXPECT_THAT(fs.FsType(Fixture()), IsOkAndHolds("squashfs"));
  EXPECT_THAT(fs.IsCaseSensitive(Fixture()), IsOkAndHolds(IsTrue()));
  EXPECT_THAT(
      fs.Remove(Fixture() + "!hello.txt"), StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("read-only")));
  EXPECT_THAT(
      fs.ReadContent(Fixture() + "!dir"), StatusIs(absl::StatusCode::kFailedPrecondition, HasSubstr("not a regular")));
  EXPECT_THAT(fs.ReadContent(Fixture() + "!missing"), StatusIs(absl::StatusCode::kNotFound, HasSubstr("missing")));
  EXPECT_THAT(
      fs.ReadLink(Fixture() + "!hello.txt"), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("not a symlink")));
  EXPECT_THAT(fs.ReadLink(Fixture() + "!missing"), StatusIs(absl::StatusCode::kNotFound, HasSubstr("missing")));
  EXPECT_THAT(fs.ReadLink(Fixture()), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("not a SquashFS member")));
  EXPECT_THAT(
      fs.ReadDir(Fixture() + "!hello.txt"), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("not a directory")));
  EXPECT_THAT(
      fs.Stat("somewhere-else", /*follow_symlinks=*/false),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("not a path")));
  EXPECT_THAT(
      fs.Stat(Fixture() + "!missing", /*follow_symlinks=*/false),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("missing")));
  EXPECT_THAT(
      fs.Stat(Fixture(), /*follow_symlinks=*/false),
      IsOkAndHolds(Field("type", &vfs::Metadata::type, vfs::FileType::kDirectory)));
  EXPECT_THAT(fs.ReadDir(Fixture() + "!missing"), StatusIs(absl::StatusCode::kNotFound, HasSubstr("missing")));
  EXPECT_THAT(
      fs.ReadContent("somewhere-else"),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("not a SquashFS member")));
}

TEST_F(SquashfsReaderTest, HonorsConfiguredMemberPathSpelling) {
  MBO_ASSERT_OK_AND_ASSIGN(const SquashfsFileSystem fs, SquashfsFileSystem::Open(Fixture(), {.separator = "#"}));
  EXPECT_THAT(
      fs.ReadDir(Fixture()), IsOkAndHolds(Contains(Field("path", &vfs::Entry::path, Fixture() + "#hello.txt"))));
  EXPECT_THAT(fs.ReadContent(Fixture() + "#hello.txt"), IsOkAndHolds("hello from squashfs\n"));
}

}  // namespace
}  // namespace xff::squashfs
