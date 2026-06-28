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

#include "xff/vfs/local_fs.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/vfs/entry.h"

namespace xff::vfs {
namespace {

namespace fs = std::filesystem;

using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Gt;
using ::testing::Not;
using ::testing::UnorderedElementsAre;

// Builds a small tree under a per-test temp directory:
//   <root>/file.txt   ("hello", 5 bytes)
//   <root>/sub/       (directory)
//   <root>/link       (symlink -> file.txt)
struct LocalFsTest : ::testing::Test {
  void SetUp() override {
    root_ = fs::path(::testing::TempDir())
            / (std::string("xff_localfs_") + ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::error_code ec;
    fs::remove_all(root_, ec);
    ASSERT_TRUE(fs::create_directories(root_));
    { std::ofstream(root_ / "file.txt") << "hello"; }
    fs::create_directory(root_ / "sub");
    fs::create_symlink("file.txt", root_ / "link");
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }

  std::string Path(std::string_view child) const { return (root_ / child).string(); }

  LocalFs local_fs_;
  fs::path root_;
};

TEST_F(LocalFsTest, ReadDirListsChildren) {
  const auto entries = local_fs_.ReadDir(root_.string());
  ASSERT_THAT(entries, IsOk());
  EXPECT_THAT(
      *entries,
      UnorderedElementsAre(
          Field(&Entry::name, Eq("file.txt")), Field(&Entry::name, Eq("sub")), Field(&Entry::name, Eq("link"))));
}

TEST_F(LocalFsTest, ReadDirTagsEntriesAsWritableLocal) {
  const auto entries = local_fs_.ReadDir(root_.string());
  ASSERT_THAT(entries, IsOk());
  for (const Entry& entry : *entries) {
    EXPECT_THAT(entry.source, Source::kLocalFs);
    EXPECT_FALSE(entry.read_only);
    EXPECT_THAT(entry.path, Path(entry.name));
  }
}

TEST_F(LocalFsTest, ReadDirOnMissingPathIsNotFound) {
  EXPECT_THAT(local_fs_.ReadDir(Path("nope")), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(LocalFsTest, ReadDirOnRegularFileFails) {
  EXPECT_THAT(local_fs_.ReadDir(Path("file.txt")), Not(IsOk()));
}

TEST_F(LocalFsTest, StatRegularFile) {
  const auto md = local_fs_.Stat(Path("file.txt"), /*follow_symlinks=*/false);
  ASSERT_THAT(md, IsOk());
  EXPECT_THAT(md->type, FileType::kRegular);
  EXPECT_THAT(md->size, 5U);
  EXPECT_THAT(md->nlink, Gt(0U));
  // mtime is populated (well after 2020-01-01, not a zero/epoch default).
  EXPECT_THAT(md->mtime, Gt(absl::FromUnixSeconds(1'577'836'800)));
}

TEST_F(LocalFsTest, StatDirectory) {
  const auto md = local_fs_.Stat(Path("sub"), /*follow_symlinks=*/false);
  ASSERT_THAT(md, IsOk());
  EXPECT_THAT(md->type, FileType::kDirectory);
}

TEST_F(LocalFsTest, StatSymlinkRespectsFollow) {
  const auto link = local_fs_.Stat(Path("link"), /*follow_symlinks=*/false);
  ASSERT_THAT(link, IsOk());
  EXPECT_THAT(link->type, FileType::kSymlink);

  const auto target = local_fs_.Stat(Path("link"), /*follow_symlinks=*/true);
  ASSERT_THAT(target, IsOk());
  EXPECT_THAT(target->type, FileType::kRegular);
  EXPECT_THAT(target->size, 5U);
}

TEST_F(LocalFsTest, StatMissingPathIsNotFound) {
  EXPECT_THAT(local_fs_.Stat(Path("nope"), /*follow_symlinks=*/false), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(LocalFsTest, RemoveDeletesFileAndEmptyDirectory) {
  EXPECT_THAT(local_fs_.Remove(Path("file.txt")), IsOk());
  EXPECT_THAT(local_fs_.Stat(Path("file.txt"), /*follow_symlinks=*/false), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(local_fs_.Remove(Path("sub")), IsOk());  // sub is an empty directory
  EXPECT_THAT(local_fs_.Stat(Path("sub"), /*follow_symlinks=*/false), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(LocalFsTest, RemoveMissingPathErrors) {
  EXPECT_THAT(local_fs_.Remove(Path("nope")), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(LocalFsTest, ReadContentReturnsFileBytes) {
  EXPECT_THAT(local_fs_.ReadContent(Path("file.txt")), IsOkAndHolds(Eq("hello")));
}

TEST_F(LocalFsTest, ReadContentMissingPathErrors) {
  EXPECT_THAT(local_fs_.ReadContent(Path("nope")), StatusIs(absl::StatusCode::kNotFound));
}

}  // namespace
}  // namespace xff::vfs
