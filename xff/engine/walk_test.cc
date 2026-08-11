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

#include <array>
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

// gtest struct-fixture idioms suppressed file-wide: SetUp/TearDown are public overrides of the
// base's protected hooks; fixture state carries the private-style `_` suffix; and the data-heavy
// TEST_F bodies plus trivial local names (a one-line path lambda) trip cognitive-complexity and
// identifier-length. All are test conventions, not defects.
// NOLINTBEGIN(misc-override-with-different-visibility,readability-identifier-naming,readability-function-cognitive-complexity,readability-identifier-length)

namespace fs = std::filesystem;

using ::mbo::testing::IsOk;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Not;
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

  absl::StatusOr<std::string> FsType(std::string_view) const override {
    return std::string("fakefs");
  }  // unused by walk tests

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }  // unused by walk tests

  absl::StatusOr<std::string> ReadContent(std::string_view) const override {
    return std::string();
  }  // unused by walk tests

 private:
  static vfs::Metadata Meta(vfs::FileType type, std::uint64_t dev, const std::string& path) {
    vfs::Metadata md;
    md.type = type;
    md.dev = dev;
    md.ino = std::hash<std::string>{}(path);  // distinct per path, for loop detection
    return md;
  }

  // Resolves one symlink level (sufficient for these tests).
  std::string Resolve(const std::string& path) const {
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

  Result Run(const WalkOptions& options, absl::FunctionRef<WalkAction(const Visit&)> control) const {
    return RunRoots({root_.string()}, options, control);
  }

  Result RunRoots(
      const std::vector<std::string>& roots,
      const WalkOptions& options,
      absl::FunctionRef<WalkAction(const Visit&)> control) const {
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

TEST_F(WalkTest, ParallelVisitsWholeTreeAsSet) {
  const Result result = Run(WalkOptions{.workers = 8}, Continue);
  EXPECT_THAT(result.status, IsOk());
  EXPECT_THAT(result.errors, 0);
  EXPECT_THAT(
      result.seen, UnorderedElementsAre(
                       Pair(root_.string(), 0), Pair(Path("a.txt"), 1), Pair(Path("sub"), 1),
                       Pair(Path("sub/b.txt"), 2), Pair(Path("link"), 1)));
}

TEST_F(WalkTest, SortModesOrderUnderWorkers) {
  // A tree where a file (`z.txt`) sorts after the subdirectories, so the three
  // ordered modes are all distinct. Output is deterministic at any worker count.
  ASSERT_TRUE(fs::create_directories(root_ / "order" / "m1"));
  ASSERT_TRUE(fs::create_directories(root_ / "order" / "m2"));
  { std::ofstream(root_ / "order" / "a.txt") << "a"; }
  { std::ofstream(root_ / "order" / "z.txt") << "z"; }
  { std::ofstream(root_ / "order" / "m1" / "x.txt") << "x"; }
  { std::ofstream(root_ / "order" / "m2" / "y.txt") << "y"; }
  const std::string base = (root_ / "order").string();
  const auto p = [&](const std::string& rel) { return (root_ / "order" / rel).string(); };
  const auto paths = [](const Result& result) {
    std::vector<std::string> out;
    out.reserve(result.seen.size());
    for (const auto& [path, depth] : result.seen) {
      out.push_back(path);
    }
    return out;
  };

  // Sequential and parallel: the ordering guarantee must not depend on the worker count.
  static constexpr std::array kWorkerCounts = std::to_array<std::size_t>({1, 4});
  for (const std::size_t workers : kWorkerCounts) {
    EXPECT_THAT(
        paths(RunRoots({base}, WalkOptions{.sort = SortOrder::kTree, .workers = workers}, Continue)),
        ElementsAre(base, p("a.txt"), p("m1"), p("m1/x.txt"), p("m2"), p("m2/y.txt"), p("z.txt")))
        << "kTree, workers=" << workers;
    EXPECT_THAT(
        paths(RunRoots({base}, WalkOptions{.sort = SortOrder::kDir, .workers = workers}, Continue)),
        ElementsAre(base, p("a.txt"), p("m1"), p("m2"), p("z.txt"), p("m1/x.txt"), p("m2/y.txt")))
        << "kDir, workers=" << workers;
    EXPECT_THAT(
        paths(RunRoots({base}, WalkOptions{.sort = SortOrder::kSubtree, .workers = workers}, Continue)),
        ElementsAre(base, p("a.txt"), p("z.txt"), p("m1"), p("m1/x.txt"), p("m2"), p("m2/y.txt")))
        << "kSubtree, workers=" << workers;
  }
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
  ASSERT_THAT(result.seen, Not(IsEmpty()));
  EXPECT_THAT(result.seen.back(), Pair(root_.string(), 0));
  const auto index_of = [&](std::string_view path) -> int {
    for (std::size_t i = 0; i < result.seen.size(); ++i) {
      if (result.seen[i].first == path) {
        return static_cast<int>(i);
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
          out.emplace_back(visit.path);
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

TEST_F(WalkFakeFsTest, IgnoreReaddirRaceSkipsVanishedEntries) {
  // /r lists gone.txt, but no node exists for it -- it vanished between readdir
  // and stat, so the stat fails with NotFound (the readdir race).
  fs_.AddDir("/r", 1, {FileEntry("/r/gone.txt", "gone.txt")});
  Seen(WalkOptions{});
  EXPECT_THAT(errors_, 1) << "by default the vanished entry's failed stat is reported";
  Seen(WalkOptions{.ignore_readdir_race = true});
  EXPECT_THAT(errors_, 0) << "-ignore_readdir_race silently skips the ENOENT race";
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

// NOLINTEND(misc-override-with-different-visibility,readability-identifier-naming,readability-function-cognitive-complexity,readability-identifier-length)

// Archive diving. The walk knows nothing about archives beyond calling the mounter, so the container's
// contents are just another FakeFs - what is under test is the WALK's half of the contract.
struct WalkMountTest : ::testing::Test {
  // A mounted filesystem holding `a.tar!one.txt`, `a.tar!dir` and `a.tar!dir/two.txt`, with the member
  // identities a real archive filesystem provides: one device, distinct inodes.
  static std::unique_ptr<const vfs::FileSystem> MountedTar() {
    auto mounted = std::make_unique<FakeFs>();
    mounted->AddDir("a.tar", 99, {Entry("a.tar!dir", vfs::FileType::kDirectory), Entry("a.tar!one.txt")});
    mounted->AddFile("a.tar!one.txt", 99);
    mounted->AddDir("a.tar!dir", 99, {Entry("a.tar!dir/two.txt")});
    mounted->AddFile("a.tar!dir/two.txt", 99);
    return mounted;
  }

  static vfs::Entry Entry(std::string path, vfs::FileType type = vfs::FileType::kRegular) {
    const std::string::size_type slash = path.find_last_of("/!");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    return vfs::Entry{.path = std::move(path), .name = std::move(name), .type = type};
  }

  // Walks `roots` with diving armed, recording each visit and counting reported errors.
  std::vector<std::pair<std::string, int>> Walked(
      const std::vector<std::string>& roots,
      const WalkOptions& options,
      absl::FunctionRef<WalkAction(const Visit&)> control,
      ContainerMounter mount) {
    std::vector<std::pair<std::string, int>> seen;
    const absl::Status status = Walk(
        fs_, roots, options,
        [&](const Visit& visit) {
          seen.emplace_back(std::string(visit.path), visit.depth);
          return control(visit);
        },
        [&](std::string_view, absl::Status) { ++errors_; }, mount);
    EXPECT_THAT(status, IsOk());
    return seen;
  }

  static WalkAction Keep(const Visit&) { return WalkAction::kContinue; }

  FakeFs fs_;
  int errors_ = 0;
};

TEST_F(WalkMountTest, AContainerNamedAsARootIsWalkedAsWellAsVisited) {
  // Dual identity: the container is still visited as the FILE it is, and its members follow one level
  // below - so an expression matching the tar by name or size behaves exactly as without diving.
  fs_.AddFile("a.tar", 1);
  EXPECT_THAT(
      Walked(
          {"a.tar"}, WalkOptions{.sort = SortOrder::kDir, .archive = ArchiveDive::kRoots}, Keep,
          [](std::string_view) { return absl::StatusOr<std::unique_ptr<const vfs::FileSystem>>(MountedTar()); }),
      // kDir order: a level's entries first, then each subtree - the members obey the same --sort the
      // rest of the walk does, because they go through the same child handling.
      ElementsAre(Pair("a.tar", 0), Pair("a.tar!dir", 1), Pair("a.tar!one.txt", 1), Pair("a.tar!dir/two.txt", 2)));
  EXPECT_THAT(errors_, 0);
}

TEST_F(WalkMountTest, NotAnArchiveIsNotAnError) {
  // InvalidArgument is the answer for every ordinary file the walk offers, so it must be silent: the
  // file was already visited as itself, and reporting here would make every plain file an error.
  fs_.AddFile("notes.txt", 1);
  EXPECT_THAT(
      Walked(
          {"notes.txt"}, WalkOptions{.archive = ArchiveDive::kRoots}, Keep,
          [](std::string_view) {
            return absl::StatusOr<std::unique_ptr<const vfs::FileSystem>>(absl::InvalidArgumentError("not an archive"));
          }),
      ElementsAre(Pair("notes.txt", 0)));
  EXPECT_THAT(errors_, 0);
}

TEST_F(WalkMountTest, AnUnreadableArchiveIsReported) {
  // The other half of that contract: a file that IS an archive but cannot be read is a real error.
  fs_.AddFile("broken.tar", 1);
  EXPECT_THAT(
      Walked(
          {"broken.tar"}, WalkOptions{.archive = ArchiveDive::kRoots}, Keep,
          [](std::string_view) {
            return absl::StatusOr<std::unique_ptr<const vfs::FileSystem>>(absl::DataLossError("corrupt"));
          }),
      ElementsAre(Pair("broken.tar", 0)));
  EXPECT_THAT(errors_, 1);
}

TEST_F(WalkMountTest, RootsModeDoesNotDiveIntoArchivesFoundMidWalk) {
  // `roots` means "the archive I pointed you AT", not every archive in the tree - that is `all`, a
  // later slice. Counting mounts proves the gate, not just the output.
  fs_.AddDir("top", 1, {Entry("top/a.tar")});
  fs_.AddFile("top/a.tar", 1);
  int mounts = 0;
  EXPECT_THAT(
      Walked(
          {"top"}, WalkOptions{.archive = ArchiveDive::kRoots}, Keep,
          [&mounts](std::string_view) {
            ++mounts;
            return absl::StatusOr<std::unique_ptr<const vfs::FileSystem>>(MountedTar());
          }),
      ElementsAre(Pair("top", 0), Pair("top/a.tar", 1)));
  EXPECT_THAT(mounts, 0);
}

TEST_F(WalkMountTest, PruningTheContainerSkipsItsMembersButKeepsTheFile) {
  // `-name '*.tar' -prune` must skip the diving without hiding the tar itself, exactly as pruning a
  // directory keeps the directory.
  fs_.AddFile("a.tar", 1);
  EXPECT_THAT(
      Walked(
          {"a.tar"}, WalkOptions{.archive = ArchiveDive::kRoots}, [](const Visit&) { return WalkAction::kPrune; },
          [](std::string_view) { return absl::StatusOr<std::unique_ptr<const vfs::FileSystem>>(MountedTar()); }),
      ElementsAre(Pair("a.tar", 0)));
}

TEST_F(WalkMountTest, QuittingInsideAnArchiveStopsTheWholeWalk) {
  // -quit is global: stopping inside a container must not merely end that container's listing.
  fs_.AddFile("a.tar", 1);
  fs_.AddFile("b.txt", 1);
  EXPECT_THAT(
      Walked(
          {"a.tar", "b.txt"}, WalkOptions{.sort = SortOrder::kDir, .archive = ArchiveDive::kRoots},
          [](const Visit& visit) { return visit.path == "a.tar!dir" ? WalkAction::kStop : WalkAction::kContinue; },
          [](std::string_view) { return absl::StatusOr<std::unique_ptr<const vfs::FileSystem>>(MountedTar()); }),
      ElementsAre(Pair("a.tar", 0), Pair("a.tar!dir", 1)));
}

TEST_F(WalkMountTest, MaxDepthCountsMemberLevels) {
  // Members are ordinary depth: -maxdepth 1 stops at the container's immediate members.
  fs_.AddFile("a.tar", 1);
  EXPECT_THAT(
      Walked(
          {"a.tar"}, WalkOptions{.max_depth = 1, .sort = SortOrder::kDir, .archive = ArchiveDive::kRoots}, Keep,
          [](std::string_view) { return absl::StatusOr<std::unique_ptr<const vfs::FileSystem>>(MountedTar()); }),
      ElementsAre(Pair("a.tar", 0), Pair("a.tar!dir", 1), Pair("a.tar!one.txt", 1)));
}

}  // namespace
}  // namespace xff::engine
