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
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"
#include "xff/vfs/local_fs.h"

namespace xff::engine {
namespace {

namespace fs = std::filesystem;

using ::mbo::testing::IsOk;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

// In-memory FileSystem for tests that need metadata the real filesystem won't
// reproduce locally (device ids for -xdev, symlink targets / loops for -L/-H).
// Nodes are added explicitly; each path gets a distinct inode for loop detection.
class FakeFs : public vfs::FileSystem {
 public:
  void AddDir(const std::string& path, std::uint64_t dev, std::vector<vfs::Entry> children) {
    nodes_[path] = Meta(vfs::FileType::kDirectory, dev, path);
    dirs_[path] = std::move(children);
  }

  void AddFile(const std::string& path, std::uint64_t dev) { nodes_[path] = Meta(vfs::FileType::kRegular, dev, path); }

  // A symlink with its own (link) metadata that resolves to `target` when followed.
  void AddSymlink(const std::string& path, std::uint64_t dev, const std::string& target) {
    nodes_[path] = Meta(vfs::FileType::kSymlink, dev, path);
    targets_[path] = target;
  }

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view dir) const override {
    const auto it = dirs_.find(Resolve(std::string(dir)));
    if (it == dirs_.end()) {
      return absl::NotFoundError("FakeFs: no such directory");
    }
    return it->second;
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool follow_symlinks) const override {
    const auto it = nodes_.find(follow_symlinks ? Resolve(std::string(path)) : std::string(path));
    if (it == nodes_.end()) {
      return absl::NotFoundError("FakeFs: no such path");
    }
    return it->second;
  }

  absl::Status Remove(std::string_view) const override { return absl::OkStatus(); }  // unused by walk tests

  bool Access(std::string_view, vfs::AccessMode) const override { return true; }  // unused by walk tests

  absl::StatusOr<std::string> ReadLink(std::string_view path) const override {
    const auto it = targets_.find(std::string(path));
    if (it == targets_.end()) {
      return absl::InvalidArgumentError("FakeFs: not a symlink");
    }
    return it->second;
  }

 private:
  static vfs::Metadata Meta(vfs::FileType type, std::uint64_t dev, const std::string& path) {
    vfs::Metadata md;
    md.type = type;
    md.dev = dev;
    md.ino = std::hash<std::string>{}(path);  // distinct per path, for loop detection
    return md;
  }

  // Resolves one symlink level (sufficient for these tests).
  std::string Resolve(std::string path) const {
    const auto it = targets_.find(path);
    return it == targets_.end() ? path : it->second;
  }

  std::map<std::string, vfs::Metadata> nodes_;
  std::map<std::string, std::vector<vfs::Entry>> dirs_;
  std::map<std::string, std::string> targets_;
};

vfs::Entry DirEntry(const std::string& path, const std::string& name) {
  return vfs::Entry{.path = path, .name = name, .type = vfs::FileType::kDirectory};
}

vfs::Entry FileEntry(const std::string& path, const std::string& name) {
  return vfs::Entry{.path = path, .name = name, .type = vfs::FileType::kRegular};
}

vfs::Entry SymlinkEntry(const std::string& path, const std::string& name) {
  return vfs::Entry{.path = path, .name = name, .type = vfs::FileType::kSymlink};
}

// Fixture tree:
//   <root>/a.txt
//   <root>/sub/b.txt
//   <root>/link -> a.txt
struct WalkTest : ::testing::Test {
  void SetUp() override {
    root_ = fs::path(::testing::TempDir())
            / (std::string("xff_walk_") + ::testing::UnitTest::GetInstance()->current_test_info()->name());
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
        [&](std::string_view, absl::Status) { ++result.errors; });
    return result;
  }

  vfs::LocalFs fs_;
  fs::path root_;
};

WalkAction Continue(const Visit& /*visit*/) {
  return WalkAction::kContinue;
}

TEST_F(WalkTest, VisitsWholeTreePreorder) {
  const Result result = Run(WalkOptions{}, Continue);
  EXPECT_THAT(result.status, IsOk());
  EXPECT_THAT(result.errors, 0);
  EXPECT_THAT(
      result.seen, UnorderedElementsAre(
                       Pair(root_.string(), 0), Pair(Path("a.txt"), 1), Pair(Path("sub"), 1),
                       Pair(Path("sub/b.txt"), 2), Pair(Path("link"), 1)));
}

TEST_F(WalkTest, MaxDepthLimitsDescent) {
  const Result result = Run(WalkOptions{.min_depth = 0, .max_depth = 1}, Continue);
  EXPECT_THAT(
      result.seen, UnorderedElementsAre(
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
      result.seen, UnorderedElementsAre(
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
  EXPECT_THAT(result.errors, 1);
}

TEST_F(WalkTest, DepthVisitsPostOrder) {
  const Result result = Run(WalkOptions{.post_order = true}, Continue);
  EXPECT_THAT(result.status, IsOk());
  // Post-order visits the same set as pre-order...
  EXPECT_THAT(
      result.seen, UnorderedElementsAre(
                       Pair(root_.string(), 0), Pair(Path("a.txt"), 1), Pair(Path("sub"), 1),
                       Pair(Path("sub/b.txt"), 2), Pair(Path("link"), 1)));
  // ...but a directory comes after its contents: the root is visited last, and
  // sub/b.txt precedes sub. Sibling order is filesystem-defined, so only the
  // parent-after-child relationship is asserted.
  ASSERT_FALSE(result.seen.empty());
  EXPECT_THAT(result.seen.back(), Pair(root_.string(), 0));
  const auto index_of = [&](std::string_view path) -> int {
    for (int i = 0; i < static_cast<int>(result.seen.size()); ++i) {
      if (result.seen[i].first == path) {
        return i;
      }
    }
    return -1;
  };
  EXPECT_LT(index_of(Path("sub/b.txt")), index_of(Path("sub")));
}

struct WalkFakeFsTest : ::testing::Test {
  std::vector<std::string> Seen(const WalkOptions& options) {
    std::vector<std::string> out;
    errors_ = 0;
    const absl::Status status = Walk(
        fs_, {"/r"}, options,
        [&](const Visit& visit) {
          out.push_back(std::string(visit.path));
          return WalkAction::kContinue;
        },
        [&](std::string_view, absl::Status) { ++errors_; });
    EXPECT_THAT(status, IsOk());
    return out;
  }

  FakeFs fs_;
  int errors_ = 0;
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

TEST_F(WalkFakeFsTest, FollowAllDescendsSymlinkedDirectory) {
  // /r holds a regular file and lnk -> /t (an out-of-tree directory holding g).
  fs_.AddDir("/r", 1, {FileEntry("/r/a", "a"), SymlinkEntry("/r/lnk", "lnk")});
  fs_.AddFile("/r/a", 1);
  fs_.AddSymlink("/r/lnk", 1, "/t");
  fs_.AddDir("/t", 1, {FileEntry("/t/g", "g")});
  fs_.AddFile("/t/g", 1);
  // -P (default): lnk is a leaf symlink; its target is not entered.
  EXPECT_THAT(Seen(WalkOptions{}), UnorderedElementsAre("/r", "/r/a", "/r/lnk"));
  // -L: lnk resolves to the directory /t and is descended into.
  EXPECT_THAT(Seen(WalkOptions{.symlinks = SymlinkMode::kAll}), UnorderedElementsAre("/r", "/r/a", "/r/lnk", "/t/g"));
}

TEST_F(WalkFakeFsTest, FollowRootsOnlyFollowsTheOperand) {
  // The root operand /r is a symlink to /real; -H follows it, but not the symlink
  // /real/lnk encountered during descent.
  fs_.AddSymlink("/r", 1, "/real");
  fs_.AddDir("/real", 1, {FileEntry("/real/a", "a"), SymlinkEntry("/real/lnk", "lnk")});
  fs_.AddFile("/real/a", 1);
  fs_.AddSymlink("/real/lnk", 1, "/other");
  fs_.AddDir("/other", 1, {FileEntry("/other/z", "z")});
  fs_.AddFile("/other/z", 1);
  EXPECT_THAT(Seen(WalkOptions{.symlinks = SymlinkMode::kRoots}), UnorderedElementsAre("/r", "/real/a", "/real/lnk"));
}

TEST_F(WalkFakeFsTest, FollowAllDetectsFilesystemLoop) {
  // /r/loop is a symlink back to /r; following it under -L re-enters an ancestor.
  fs_.AddDir("/r", 1, {SymlinkEntry("/r/loop", "loop")});
  fs_.AddSymlink("/r/loop", 1, "/r");
  EXPECT_THAT(Seen(WalkOptions{.symlinks = SymlinkMode::kAll}), UnorderedElementsAre("/r", "/r/loop"));
  EXPECT_THAT(errors_, 1);  // the loop was reported, and the walk did not recurse forever
}

TEST_F(WalkFakeFsTest, CarriesOriginatingRootPerEntry) {
  // Two command-line roots; every visited entry reports the root it descends from
  // (find %H): a root operand reports itself, and descendants inherit it.
  fs_.AddDir("/r", 1, {FileEntry("/r/a", "a"), DirEntry("/r/sub", "sub")});
  fs_.AddFile("/r/a", 1);
  fs_.AddDir("/r/sub", 1, {FileEntry("/r/sub/b", "b")});
  fs_.AddFile("/r/sub/b", 1);
  fs_.AddDir("/s", 1, {FileEntry("/s/c", "c")});
  fs_.AddFile("/s/c", 1);

  std::vector<std::pair<std::string, std::string>> seen;  // (path, root)
  const absl::Status status = Walk(
      fs_, {"/r", "/s"}, WalkOptions{},
      [&](const Visit& visit) {
        seen.emplace_back(std::string(visit.path), std::string(visit.root));
        return WalkAction::kContinue;
      },
      [&](std::string_view, absl::Status) {});
  EXPECT_THAT(status, IsOk());
  EXPECT_THAT(
      seen, UnorderedElementsAre(
                Pair("/r", "/r"), Pair("/r/a", "/r"), Pair("/r/sub", "/r"), Pair("/r/sub/b", "/r"), Pair("/s", "/s"),
                Pair("/s/c", "/s")));
}

}  // namespace
}  // namespace xff::engine
