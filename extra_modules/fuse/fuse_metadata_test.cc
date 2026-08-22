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

#include "xff/fuse/fuse_metadata.h"

#include <sys/stat.h>

#include <array>
#include <cerrno>
#include <utility>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/vfs/entry.h"

namespace xff::fuse {
namespace {

using ::testing::Eq;

struct FuseMetadataTest : ::testing::Test {};

TEST_F(FuseMetadataTest, StatusCodesMapToStableErrnos) {
  static constexpr auto kCases = std::to_array<std::pair<absl::StatusCode, int>>({
      {absl::StatusCode::kFailedPrecondition, EINVAL},
      {absl::StatusCode::kInvalidArgument, EINVAL},
      {absl::StatusCode::kNotFound, ENOENT},
      {absl::StatusCode::kPermissionDenied, EACCES},
      {absl::StatusCode::kUnimplemented, ENOTSUP},
      {absl::StatusCode::kInternal, EIO},
  });
  for (const auto& [code, expected] : kCases) {
    EXPECT_THAT(ErrnoForStatus(code), Eq(expected));
  }
}

TEST_F(FuseMetadataTest, EveryFileTypeMapsToAKernelMode) {
  static constexpr auto kCases = std::to_array<std::pair<vfs::FileType, mode_t>>({
      {vfs::FileType::kBlockDevice, S_IFBLK},
      {vfs::FileType::kCharDevice, S_IFCHR},
      {vfs::FileType::kDirectory, S_IFDIR},
      {vfs::FileType::kFifo, S_IFIFO},
      {vfs::FileType::kRegular, S_IFREG},
      {vfs::FileType::kSocket, S_IFSOCK},
      {vfs::FileType::kSymlink, S_IFLNK},
      {vfs::FileType::kUnknown, S_IFREG},
  });
  for (const auto& [type, expected] : kCases) {
    EXPECT_THAT(ModeBitsForFileType(type), Eq(expected));
  }
}

TEST_F(FuseMetadataTest, OutOfRangeFileTypeFallsBackToARegularFile) {
  EXPECT_THAT(ModeBitsForFileType(static_cast<vfs::FileType>(-1)), Eq(S_IFREG));
}

TEST_F(FuseMetadataTest, RegularMetadataBecomesReadOnlyStatData) {
  const vfs::Metadata metadata = {
      .type = vfs::FileType::kRegular,
      .size = 123,
      .blocks = 4,
      .mode = 0767,
      .nlink = 3,
  };
  const struct stat result = StatForMetadata(metadata, 42);
  EXPECT_THAT(result.st_ino, Eq(42U));
  EXPECT_THAT(result.st_mode & S_IFMT, Eq(S_IFREG));
  EXPECT_THAT(static_cast<unsigned int>(result.st_mode) & 0777U, Eq(0545U));
  EXPECT_THAT(result.st_nlink, Eq(3U));
  EXPECT_THAT(result.st_size, Eq(123));
  EXPECT_THAT(result.st_blocks, Eq(4));
}

TEST_F(FuseMetadataTest, DirectoryDerivesSearchBitsAndDefaultsLinkCount) {
  const vfs::Metadata metadata = {
      .type = vfs::FileType::kDirectory,
      .mode = 0444,
  };
  const struct stat result = StatForMetadata(metadata, 1);
  EXPECT_THAT(result.st_mode & S_IFMT, Eq(S_IFDIR));
  EXPECT_THAT(static_cast<unsigned int>(result.st_mode) & 0777U, Eq(0555U));
  EXPECT_THAT(result.st_nlink, Eq(1U));
}

TEST_F(FuseMetadataTest, SymlinkAlwaysCarriesConventionalPermissions) {
  const vfs::Metadata metadata = {
      .type = vfs::FileType::kSymlink,
      .mode = 0,
  };
  const struct stat result = StatForMetadata(metadata, 2);
  EXPECT_THAT(result.st_mode & S_IFMT, Eq(S_IFLNK));
  EXPECT_THAT(static_cast<unsigned int>(result.st_mode) & 0777U, Eq(0777U));
}

}  // namespace
}  // namespace xff::fuse
