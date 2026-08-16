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

#include "xff/engine/mount.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;

// This binary links no fuse extra, which is the LEAN build every one of these tests describes: the
// mount seam answers "not built in", so a mount never happens and the caller extracts. The mounted
// path itself is the extra's own test (@xff_fuse), where a mount can actually be made.
struct StubFileSystem : vfs::FileSystem {
  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view /*dir*/) const override {
    return std::vector<vfs::Entry>{};
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view /*path*/, bool /*follow_symlinks*/) const override {
    return vfs::Metadata{.type = vfs::FileType::kRegular};
  }

  absl::Status Remove(std::string_view /*path*/) const override { return absl::UnimplementedError("read-only"); }

  bool Access(std::string_view /*path*/, vfs::AccessMode /*mode*/) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view /*path*/) const override {
    return absl::InvalidArgumentError("not a symlink");
  }

  absl::StatusOr<std::string> FsType(std::string_view /*path*/) const override { return std::string("fake"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view /*path*/) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view /*path*/) const override {
    return absl::NotFoundError("no content");
  }
};

struct MountedContainersTest : ::testing::Test {
  StubFileSystem fs;
};

TEST_F(MountedContainersTest, DisarmedAnswersNothingAndExplainsNothing) {
  MountedContainers mounts;
  EXPECT_THAT(mounts.Armed(), IsFalse());
  EXPECT_THAT(mounts.PathFor(fs, nullptr, "a.tar!hello.txt"), Eq(std::nullopt));
  // Disarmed is not a degrade: nothing was asked for, so there is nothing to report.
  EXPECT_THAT(mounts.DegradeReason(), Eq(""));
}

TEST_F(MountedContainersTest, APathThatNamesNoMemberIsNotMountedAndIsNotAFailure) {
  MountedContainers mounts(/*armed=*/true);
  EXPECT_THAT(mounts.Armed(), IsTrue());
  // An ordinary file: the walk hands these to the same call, and a mount would be meaningless.
  EXPECT_THAT(mounts.PathFor(fs, nullptr, "/tmp/plain.txt"), Eq(std::nullopt));
  EXPECT_THAT(mounts.DegradeReason(), Eq(""));
}

TEST_F(MountedContainersTest, WithoutTheExtraAMemberDegradesWithOneReason) {
  MountedContainers mounts(/*armed=*/true);
  EXPECT_THAT(mounts.PathFor(fs, nullptr, "a.tar!hello.txt"), Eq(std::nullopt));
  const std::string first(mounts.DegradeReason());
  EXPECT_THAT(first, Not(Eq("")));

  // A second member, and a member of a SECOND container, must not append another sentence: the run
  // reports the reason once, however many members it visits.
  EXPECT_THAT(mounts.PathFor(fs, nullptr, "a.tar!other.txt"), Eq(std::nullopt));
  EXPECT_THAT(mounts.PathFor(fs, nullptr, "b.zip!x"), Eq(std::nullopt));
  EXPECT_THAT(std::string(mounts.DegradeReason()), Eq(first));
}

}  // namespace
}  // namespace xff::engine
