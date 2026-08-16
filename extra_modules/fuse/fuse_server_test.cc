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
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::fuse {
namespace {

using ::mbo::testing::IsOk;
using ::testing::Eq;
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

// One mount per test (a mount is milliseconds), so each test states one behaviour and no test
// depends on an earlier one's reads.
struct FuseServerTest : ::testing::Test {
  FakeFileSystem fs;
  std::string mount_point;
  std::unique_ptr<FuseServer> server;

  // Mounts the fake tree, or marks the test skipped and leaves `server` null. Callers must return
  // when `server` is null: gtest's SKIP/FATAL only unwind the TEST body, never a helper.
  void MountOrSkip(std::string_view name) {
#if defined(MEMORY_SANITIZER)
    // MSan false-positives on anything it did not instrument, and these tests exist to call
    // through the dlopened SYSTEM libfuse3 - every byte it writes reads as uninitialized.
    // ASan/TSan tolerate uninstrumented libraries and keep running this path.
    GTEST_SKIP() << "MSan cannot model the uninstrumented system libfuse3";
#else
    if (!FuseAvailable()) {
      GTEST_SKIP() << "no fuse3 on this machine";
    }
    // A plain directory is all a mount point is; MountRoot (which normally provides it) has its
    // own test and stays out of these.
    mount_point = (std::filesystem::path(::testing::TempDir()) / absl::StrCat("xff-fuse-", name)).string();
    std::filesystem::remove_all(mount_point);
    std::filesystem::create_directories(mount_point);
    absl::StatusOr<std::unique_ptr<FuseServer>> mounted =
        FuseServer::Mount(fs, std::string(FakeFileSystem::kRoot), mount_point);
    if (!mounted.ok()) {
      // A library without a mountable environment (no /dev/fuse, no setuid fusermount3 - common in
      // sandboxes) is a degrade, not a failure - EXCEPT where the environment promises
      // mountability (Linux CI sets XFF_FUSE_REQUIRED): there a skip would silently retire the
      // whole kernel path.
      // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded test setup
      ASSERT_THAT(std::getenv("XFF_FUSE_REQUIRED"), IsNull()) << mounted.status();
      GTEST_SKIP() << "fuse3 present but mounting not permitted here: " << mounted.status();
    }
    server = *std::move(mounted);
#endif
  }

  // The whole content of `relative` under the mount.
  std::string ReadFile(std::string_view relative) const {
    const std::ifstream file(std::filesystem::path(mount_point) / relative);
    std::stringstream content;
    content << file.rdbuf();
    return content.str();
  }
};

TEST_F(FuseServerTest, WithoutFuseMountReportsTheLoaderReason) {
  if (FuseAvailable()) {
    GTEST_SKIP() << "this machine has fuse3; the degrade path is covered where it does not";
  }
  EXPECT_THAT(
      FuseServer::Mount(fs, std::string(FakeFileSystem::kRoot), ::testing::TempDir()),  // NL
      Not(IsOk()));
}

TEST_F(FuseServerTest, TheMountedRootListsTheContainerRoot) {
  MountOrSkip("root");
  if (server == nullptr) {
    return;
  }
  // std::string on both sides: matching a string_view SUBJECT makes gmock build its matcher on
  // the heap (the inline buffer cannot hold that impl), and the clang analyzer cannot follow that
  // refcount - it reports a leak inside gmock. Comparing strings needs no allocation at all.
  EXPECT_THAT(std::string(server->MountPoint()), Eq(mount_point));
  std::vector<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator(mount_point)) {
    names.push_back(entry.path().filename().string());
  }
  EXPECT_THAT(names, UnorderedElementsAre("hello.txt", "sub", "link"));
}

TEST_F(FuseServerTest, AMemberReadsBackThroughTheKernel) {
  MountOrSkip("read");
  if (server == nullptr) {
    return;
  }
  EXPECT_THAT(ReadFile("hello.txt"), Eq(std::string(FakeFileSystem::kHello)));
}

TEST_F(FuseServerTest, ANestedMemberReadsBackThroughTheKernel) {
  MountOrSkip("nested");
  if (server == nullptr) {
    return;
  }
  EXPECT_THAT(ReadFile("sub/a.bin"), Eq(std::string("abc")));
}

TEST_F(FuseServerTest, ASymlinkResolvesToItsTarget) {
  MountOrSkip("symlink");
  if (server == nullptr) {
    return;
  }
  std::error_code error;
  const std::filesystem::path target =
      std::filesystem::read_symlink(std::filesystem::path(mount_point) / "link", error);
  EXPECT_THAT(error.value(), Eq(0));
  EXPECT_THAT(target.string(), Eq(std::string("hello.txt")));
}

TEST_F(FuseServerTest, DestroyingTheServerUnmounts) {
  MountOrSkip("unmount");
  if (server == nullptr) {
    return;
  }
  server.reset();
  std::error_code error;
  EXPECT_THAT(std::filesystem::is_empty(mount_point, error), IsTrue());
}

TEST_F(FuseServerTest, CrashUnmountToleratesANonMount) {
  // Nothing is mounted at TempDir; the helper must swallow the tool's failure silently.
  CrashUnmount(::testing::TempDir());
}

}  // namespace
}  // namespace xff::fuse
