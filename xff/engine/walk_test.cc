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

#include "xff/engine/walk.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "mbo/testing/status.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"
#include "xff/vfs/local_fs.h"

namespace xff::engine {
namespace {

namespace fs = std::filesystem;

using ::mbo::testing::IsOk;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

// In-memory FileSystem for tests that need metadata the real filesystem won't
// reproduce locally (here: device ids for -xdev). Nodes are added explicitly.
class FakeFs : public vfs::FileSystem {
 public:
  void AddDir(const std::string& path, std::uint64_t dev, std::vector<vfs::Entry> children) {
    nodes_[path] = Meta(vfs::FileType::kDirectory, dev);
    dirs_[path] = std::move(children);
  }
  void AddFile(const std::string& path, std::uint64_t dev) { nodes_[path] = Meta(vfs::FileType::kRegular, dev); }

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view dir) const override {
    const auto it = dirs_.find(std::string(dir));
    if (it == dirs_.end()) return absl::NotFoundError("FakeFs: no such directory");
    return it->second;
  }
  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool /*follow_symlinks*/) const override {
    const auto it = nodes_.find(std::string(path));
    if (it == nodes_.end()) return absl::NotFoundError("FakeFs: no such path");
    return it->second;
  }

 private:
  static vfs::Metadata Meta(vfs::FileType type, std::uint64_t dev) {
    vfs::Metadata md;
    md.type = type;
    md.dev = dev;
    return md;
  }
  std::map<std::string, vfs::Metadata> nodes_;
  std::map<std::string, std::vector<vfs::Entry>> dirs_;
};

vfs::Entry DirEntry(const std::string& path, const std::string& name) {
  return vfs::Entry{.path = path, .name = name, .type = vfs::FileType::kDirectory};
}
vfs::Entry FileEntry(const std::string& path, const std::string& name) {
  return vfs::Entry{.path = path, .name = name, .type = vfs::FileType::kRegular};
}

// Fixture tree:
//   <root>/a.txt
//   <root>/sub/b.txt
//   <root>/link -> a.txt
struct WalkTest : ::testing::Test {
  void SetUp() override {
    root_ = fs::path(::testing::TempDir()) /
            (std::string("xff_walk_") + ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::error_code ec;
    fs::remove_all(root_, ec);
    ASSERT_TRUE(fs::create_directories(root_ / "sub"));
    { std::ofstream(root_ / "a.txt") << "a"; }
    { std::ofstream(root_ / "sub" / "b.txt") << "b"; }
    fs::create_symlink("a.txt", root_ / "link");
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }

  std::string Path(std::string_view child) const { return (root_ / child).string(); }

  struct Result {
    std::vector<std::pair<std::string, int>> seen;
    int errors = 0;
    absl::Status status;
  };

  Result Run(const WalkOptions& options, absl::FunctionRef<WalkAction(const Visit&)> control) {
    return RunRoots({root_.string()}, options, control);
  }

  Result RunRoots(
      const std::vector<std::string>& roots,
      const WalkOptions& options,
      absl::FunctionRef<WalkAction(const Visit&)> control) {
    Result result;
    result.status = Walk(
        fs_, roots, options,
        [&](const Visit& visit) {
          result.seen.emplace_back(std::string(visit.path), visit.depth);
          return control(visit);
        },
        [&](std::string_view, const absl::Status&) { ++result.errors; });
    return result;
  }

  vfs::LocalFs fs_;
  fs::path root_;
};

WalkAction Continue(const Visit& /*visit*/) { return WalkAction::kContinue; }

TEST_F(WalkTest, VisitsWholeTreePreorder) {
  const Result result = Run(WalkOptions{}, Continue);
  EXPECT_THAT(result.status, IsOk());
  EXPECT_THAT(result.errors, Eq(0));
  EXPECT_THAT(
      result.seen,
      UnorderedElementsAre(
          Pair(root_.string(), 0),
          Pair(Path("a.txt"), 1),
          Pair(Path("sub"), 1),
          Pair(Path("sub/b.txt"), 2),
          Pair(Path("link"), 1)));
}

TEST_F(WalkTest, MaxDepthLimitsDescent) {
  const Result result = Run(WalkOptions{.min_depth = 0, .max_depth = 1}, Continue);
  EXPECT_THAT(
      result.seen,
      UnorderedElementsAre(
          Pair(root_.string(), 0), Pair(Path("a.txt"), 1), Pair(Path("sub"), 1), Pair(Path("link"), 1)));
}

TEST_F(WalkTest, MaxDepthZeroVisitsRootsOnly) {
  const Result result = Run(WalkOptions{.min_depth = 0, .max_depth = 0}, Continue);
  EXPECT_THAT(result.seen, ElementsAre(Pair(root_.string(), 0)));
}

TEST_F(WalkTest, MinDepthSkipsShallowButStillDescends) {
  const Result result = Run(WalkOptions{.min_depth = 1, .max_depth = -1}, Continue);
  EXPECT_THAT(
      result.seen,
      UnorderedElementsAre(
          Pair(Path("a.txt"), 1), Pair(Path("sub"), 1), Pair(Path("sub/b.txt"), 2), Pair(Path("link"), 1)));
}

TEST_F(WalkTest, PruneSkipsDirectorySubtree) {
  const std::string sub = Path("sub");
  const Result result = Run(WalkOptions{}, [&](const Visit& visit) {
    return visit.path == sub ? WalkAction::kPrune : WalkAction::kContinue;
  });
  EXPECT_THAT(
      result.seen,
      UnorderedElementsAre(
          Pair(root_.string(), 0), Pair(Path("a.txt"), 1), Pair(Path("sub"), 1), Pair(Path("link"), 1)));
}

TEST_F(WalkTest, StopHaltsWalk) {
  const Result result = Run(WalkOptions{}, [](const Visit& /*visit*/) { return WalkAction::kStop; });
  EXPECT_THAT(result.status, IsOk());
  EXPECT_THAT(result.seen, ElementsAre(Pair(root_.string(), 0)));
}

TEST_F(WalkTest, MissingRootReportsErrorAndContinues) {
  const Result result = RunRoots({Path("does-not-exist")}, WalkOptions{}, Continue);
  EXPECT_THAT(result.status, IsOk());
  EXPECT_THAT(result.seen, IsEmpty());
  EXPECT_THAT(result.errors, Eq(1));
}

TEST_F(WalkTest, DepthVisitsPostOrder) {
  const Result result = Run(WalkOptions{.post_order = true}, Continue);
  EXPECT_THAT(result.status, IsOk());
  // Post-order visits the same set as pre-order...
  EXPECT_THAT(
      result.seen,
      UnorderedElementsAre(
          Pair(root_.string(), 0),
          Pair(Path("a.txt"), 1),
          Pair(Path("sub"), 1),
          Pair(Path("sub/b.txt"), 2),
          Pair(Path("link"), 1)));
  // ...but a directory comes after its contents: the root is visited last, and
  // sub/b.txt precedes sub. Sibling order is filesystem-defined, so only the
  // parent-after-child relationship is asserted.
  ASSERT_FALSE(result.seen.empty());
  EXPECT_THAT(result.seen.back(), Pair(root_.string(), 0));
  const auto index_of = [&](std::string_view path) -> int {
    for (int i = 0; i < static_cast<int>(result.seen.size()); ++i) {
      if (result.seen[i].first == path) return i;
    }
    return -1;
  };
  EXPECT_LT(index_of(Path("sub/b.txt")), index_of(Path("sub")));
}

struct WalkFakeFsTest : ::testing::Test {
  std::vector<std::string> Seen(const WalkOptions& options) {
    std::vector<std::string> out;
    const absl::Status status = Walk(
        fs_, {"/r"}, options,
        [&](const Visit& visit) {
          out.push_back(std::string(visit.path));
          return WalkAction::kContinue;
        },
        [](std::string_view, const absl::Status&) {});
    EXPECT_THAT(status, IsOk());
    return out;
  }

  FakeFs fs_;
};

TEST_F(WalkFakeFsTest, XdevStopsAtDeviceBoundary) {
  // /r (dev 1) holds a.txt (dev 1) and the mount point mnt (dev 2), whose child
  // x.txt (dev 2) lives on the other filesystem.
  fs_.AddDir("/r", 1, {FileEntry("/r/a.txt", "a.txt"), DirEntry("/r/mnt", "mnt")});
  fs_.AddFile("/r/a.txt", 1);
  fs_.AddDir("/r/mnt", 2, {FileEntry("/r/mnt/x.txt", "x.txt")});
  fs_.AddFile("/r/mnt/x.txt", 2);
  // Default crosses the boundary; -xdev visits the mount point but not its contents.
  EXPECT_THAT(Seen(WalkOptions{}), UnorderedElementsAre("/r", "/r/a.txt", "/r/mnt", "/r/mnt/x.txt"));
  EXPECT_THAT(Seen(WalkOptions{.single_filesystem = true}), UnorderedElementsAre("/r", "/r/a.txt", "/r/mnt"));
}

}  // namespace
}  // namespace xff::engine
