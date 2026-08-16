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

#include "xff/fuse/fuse_server.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/fuse/fuse_loader.h"
#include "xff/fuse/mount_root.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::fuse {
namespace {

using ::mbo::testing::IsOk;
using ::testing::IsNull;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::UnorderedElementsAre;

// A fixed three-entry tree the way an opened container presents one:
//   <root>/hello.txt   regular, "hello, mount\n"
//   <root>/sub/        directory
//   <root>/sub/a.bin   regular, "abc"
//   <root>/link        symlink -> "hello.txt"
struct FakeFileSystem : vfs::FileSystem {
  static constexpr std::string_view kRoot = "/container.zip";
  static constexpr std::string_view kHello = "hello, mount\n";

  static vfs::Metadata MetaFor(vfs::FileType type, std::uint64_t size) {
    return {
        .type = type,
        .source = vfs::Source::kArchiveMember,
        .size = size,
        .mode = 0644,
        .mtime = absl::FromUnixSeconds(1'700'000'000),
    };
  }

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view dir) const override {
    if (dir == kRoot) {
      return std::vector<vfs::Entry>{
          {.path = absl::StrCat(kRoot, "/hello.txt"), .name = "hello.txt", .type = vfs::FileType::kRegular},
          {.path = absl::StrCat(kRoot, "/sub"), .name = "sub", .type = vfs::FileType::kDirectory},
          {.path = absl::StrCat(kRoot, "/link"), .name = "link", .type = vfs::FileType::kSymlink},
      };
    }
    if (dir == absl::StrCat(kRoot, "/sub")) {
      return std::vector<vfs::Entry>{
          {.path = absl::StrCat(kRoot, "/sub/a.bin"), .name = "a.bin", .type = vfs::FileType::kRegular},
      };
    }
    return absl::NotFoundError(absl::StrCat("no directory '", dir, "'"));
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool /*follow_symlinks*/) const override {
    if (path == kRoot || path == absl::StrCat(kRoot, "/sub")) {
      return MetaFor(vfs::FileType::kDirectory, 0);
    }
    if (path == absl::StrCat(kRoot, "/hello.txt")) {
      return MetaFor(vfs::FileType::kRegular, kHello.size());
    }
    if (path == absl::StrCat(kRoot, "/sub/a.bin")) {
      return MetaFor(vfs::FileType::kRegular, 3);
    }
    if (path == absl::StrCat(kRoot, "/link")) {
      return MetaFor(vfs::FileType::kSymlink, 9);
    }
    return absl::NotFoundError(absl::StrCat("no entry '", path, "'"));
  }

  absl::Status Remove(std::string_view /*path*/) const override { return absl::UnimplementedError("read-only"); }

  bool Access(std::string_view /*path*/, vfs::AccessMode mode) const override { return mode == vfs::AccessMode::kRead; }

  absl::StatusOr<std::string> ReadLink(std::string_view path) const override {
    if (path == absl::StrCat(kRoot, "/link")) {
      return std::string("hello.txt");
    }
    return absl::InvalidArgumentError("not a symlink");
  }

  absl::StatusOr<std::string> FsType(std::string_view /*path*/) const override { return std::string("fake"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view /*path*/) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    if (path == absl::StrCat(kRoot, "/hello.txt")) {
      return std::string(kHello);
    }
    if (path == absl::StrCat(kRoot, "/sub/a.bin")) {
      return std::string("abc");
    }
    return absl::NotFoundError(absl::StrCat("no content '", path, "'"));
  }
};

struct FuseServerTest : ::testing::Test {
  FakeFileSystem fs;
};

TEST_F(FuseServerTest, WithoutFuseMountReportsTheLoaderReason) {
  if (FuseAvailable()) {
    GTEST_SKIP() << "this machine has fuse3; the degrade path is covered where it does not";
  }
  EXPECT_THAT(
      FuseServer::Mount(fs, std::string(FakeFileSystem::kRoot), ::testing::TempDir()),  // NL
      Not(IsOk()));
}

TEST_F(FuseServerTest, MountedTreeReadsThroughTheKernel) {
  if (!FuseAvailable()) {
    GTEST_SKIP() << "no fuse3 on this machine";
  }
  MBO_ASSERT_OK_AND_ASSIGN(MountRoot root, MountRoot::Create({.base_override = ::testing::TempDir()}));
  MBO_ASSERT_OK_AND_ASSIGN(const std::string mount_point, root.MountPointFor(FakeFileSystem::kRoot));
  absl::StatusOr<std::unique_ptr<FuseServer>> server =
      FuseServer::Mount(fs, std::string(FakeFileSystem::kRoot), mount_point);
  if (!server.ok()) {
    // A library without a mountable environment (no /dev/fuse, no setuid fusermount3 - common in
    // sandboxes) is a degrade, not a failure - EXCEPT where the environment promises mountability
    // (Linux CI sets XFF_FUSE_REQUIRED): there a skip would silently retire the kernel path.
    // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded test setup
    ASSERT_THAT(std::getenv("XFF_FUSE_REQUIRED"), IsNull()) << server.status();
    GTEST_SKIP() << "fuse3 present but mounting not permitted here: " << server.status();
  }
  EXPECT_THAT((*server)->MountPoint(), mount_point);

  std::vector<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator(mount_point)) {
    names.push_back(entry.path().filename().string());
  }
  EXPECT_THAT(names, UnorderedElementsAre("hello.txt", "sub", "link"));

  std::ifstream hello(std::filesystem::path(mount_point) / "hello.txt");
  std::stringstream content;
  content << hello.rdbuf();
  EXPECT_THAT(content.str(), FakeFileSystem::kHello);

  std::ifstream nested(std::filesystem::path(mount_point) / "sub" / "a.bin");
  std::stringstream nested_content;
  nested_content << nested.rdbuf();
  EXPECT_THAT(nested_content.str(), "abc");

  std::error_code error;
  const std::filesystem::path target =
      std::filesystem::read_symlink(std::filesystem::path(mount_point) / "link", error);
  EXPECT_THAT(error.value(), 0);
  EXPECT_THAT(target.string(), "hello.txt");

  server->reset();
  // Unmounted: the directory is empty again (the fake tree is gone with the mount).
  EXPECT_THAT(std::filesystem::is_empty(mount_point, error), IsTrue());
}

TEST_F(FuseServerTest, CrashUnmountToleratesANonMount) {
  // Nothing is mounted at TempDir; the helper must swallow the tool's failure silently.
  CrashUnmount(::testing::TempDir());
}

}  // namespace
}  // namespace xff::fuse
