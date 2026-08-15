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

#include "xff/fuse/mount_root.h"

#include <unistd.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

namespace xff::fuse {
namespace {

namespace stdfs = std::filesystem;

using ::testing::AllOf;
using ::testing::EndsWith;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

struct MountRootTest : ::testing::Test {
  void SetUp() override {
    base_ = absl::StrCat(
        ::testing::TempDir(), "/mount-root-test-", ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::error_code error;
    stdfs::remove_all(base_, error);
    stdfs::create_directories(base_, error);
    options_ = MountRootOptions{.base_override = base_};
  }

  void TearDown() override {
    std::error_code error;
    stdfs::remove_all(base_, error);
  }

  std::string base_;
  MountRootOptions options_;
};

TEST_F(MountRootTest, CreateMakesThePerRunTreeAndTheDestructorRemovesIt) {
  std::string path;
  {
    MBO_ASSERT_OK_AND_ASSIGN(const MountRoot root, MountRoot::Create(options_));
    path = std::string(root.path());
    EXPECT_THAT(path, AllOf(HasSubstr(absl::StrCat(base_, "/xff/")), EndsWith(absl::StrCat("/", ::getpid()))));
    EXPECT_TRUE(stdfs::is_directory(path));
  }
  EXPECT_FALSE(stdfs::exists(path));
  // The shared `<base>/xff/` level survives the run: it is every run's parent, never one run's own.
  EXPECT_TRUE(stdfs::is_directory(absl::StrCat(base_, "/xff")));
}

TEST_F(MountRootTest, MountPointsUseTheBasenameAndDisambiguateDuplicates) {
  MBO_ASSERT_OK_AND_ASSIGN(MountRoot root, MountRoot::Create(options_));
  MBO_ASSERT_OK_AND_ASSIGN(const std::string first, root.MountPointFor("/data/box.tgz"));
  MBO_ASSERT_OK_AND_ASSIGN(const std::string second, root.MountPointFor("/other/box.tgz"));
  EXPECT_THAT(first, EndsWith("/box.tgz"));
  EXPECT_THAT(second, EndsWith("/box.tgz.1"));
  EXPECT_TRUE(stdfs::is_directory(first));
  EXPECT_TRUE(stdfs::is_directory(second));
}

TEST_F(MountRootTest, AMovedFromRootOwnsNothing) {
  MBO_ASSERT_OK_AND_ASSIGN(MountRoot root, MountRoot::Create(options_));
  const std::string path(root.path());
  {
    const MountRoot taken = std::move(root);
    EXPECT_THAT(taken.path(), path);
  }
  // `taken` removed the tree; the moved-from `root` destructing later must not touch anything.
  EXPECT_FALSE(stdfs::exists(path));
}

TEST_F(MountRootTest, StaleRootsFindsDeadPidsAndLeavesTheLivingAndTheForeign) {
  MBO_ASSERT_OK_AND_ASSIGN(const MountRoot live, MountRoot::Create(options_));
  // A pid can never be alive beyond the kernel's pid space; 999999999 is comfortably dead. A
  // non-numeric sibling is somebody else's directory and must never be considered ours.
  const std::string dead = absl::StrCat(base_, "/xff/999999999");
  const std::string foreign = absl::StrCat(base_, "/xff/not-a-pid");
  std::error_code error;
  stdfs::create_directories(dead, error);
  stdfs::create_directories(foreign, error);
  EXPECT_THAT(StaleRoots(options_), UnorderedElementsAre(dead));
}

TEST_F(MountRootTest, SweepUnmountsEachMountPointThenRemovesTheRoot) {
  const std::string dead = absl::StrCat(base_, "/xff/999999999");
  std::error_code error;
  stdfs::create_directories(absl::StrCat(dead, "/box.tgz"), error);
  stdfs::create_directories(absl::StrCat(dead, "/other.zip"), error);
  std::vector<std::string> unmounted;
  const std::size_t removed =
      SweepStaleRoots([&unmounted](std::string_view point) { unmounted.emplace_back(point); }, options_);
  EXPECT_THAT(removed, 1U);
  EXPECT_THAT(unmounted, UnorderedElementsAre(absl::StrCat(dead, "/box.tgz"), absl::StrCat(dead, "/other.zip")));
  EXPECT_FALSE(stdfs::exists(dead));
}

TEST_F(MountRootTest, SweepingNothingIsANoOp) {
  EXPECT_THAT(StaleRoots(options_), IsEmpty());
  EXPECT_THAT(SweepStaleRoots([](std::string_view) {}, options_), 0U);
}

}  // namespace
}  // namespace xff::fuse
