// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status_macros.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_backend.h"
#include "xff/vfs/filesystem.h"

namespace xff::squashfs {
namespace {

using ::mbo::testing::IsOkAndHolds;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Field;
using ::testing::IsTrue;
using ::testing::NotNull;

struct SquashfsRegisterTest : ::testing::Test {};

TEST_F(SquashfsRegisterTest, LinkingTheExtraAddsSquashfsAndSnapToTheArchiveVocabulary) {
  EXPECT_THAT(archive::ContainerSupportAvailable(), IsTrue());
  EXPECT_THAT(archive::LooksLikeContainerName("filesystem.sqfs"), IsTrue());
  EXPECT_THAT(archive::LooksLikeContainerName("application.snap"), IsTrue());
  EXPECT_THAT(archive::LooksLikeContainerName("application.AppImage"), IsTrue());
  EXPECT_THAT(
      archive::ContainerReadFormats(),
      Contains(AllOf(
          Field("name", &archive::ReadFormatInfo::name, "squashfs"),
          Field("suffixes", &archive::ReadFormatInfo::suffixes, Contains(".squashfs")))));
}

TEST_F(SquashfsRegisterTest, TheArchiveFilesystemRoutesMemberOperationsBackToSquashfs) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): Bazel supplies this immutable value before the test starts.
  const char* const fixture = std::getenv("XFF_SQUASHFS_FIXTURE");
  ASSERT_THAT(fixture, NotNull());
  MBO_ASSERT_OK_AND_ASSIGN(std::unique_ptr<vfs::FileSystem> fs, archive::OpenContainer(fixture));
  EXPECT_THAT(fs->ReadContent(std::string(fixture) + "!hello.txt"), IsOkAndHolds("hello from squashfs\n"));
  EXPECT_THAT(
      fs->ReadDir(std::string(fixture) + "!dir/sub"),
      IsOkAndHolds(Contains(Field("name", &vfs::Entry::name, "data.bin"))));
}

TEST_F(SquashfsRegisterTest, AContainerMemberCanBeOpenedFromRetainedBytes) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): Bazel supplies this immutable value before the test starts.
  const char* const fixture = std::getenv("XFF_SQUASHFS_FIXTURE");
  ASSERT_THAT(fixture, NotNull());
  std::ifstream input(fixture, std::ios::binary);
  const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  MBO_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<vfs::FileSystem> fs, archive::OpenContainerBytes("outer.tar!inner.sqfs", bytes));
  EXPECT_THAT(fs->ReadContent("outer.tar!inner.sqfs!hello.txt"), IsOkAndHolds("hello from squashfs\n"));
}

}  // namespace
}  // namespace xff::squashfs
