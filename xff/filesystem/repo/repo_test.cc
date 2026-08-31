// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
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

#include "xff/filesystem/repo/repo.h"

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::repo {
namespace {

using ::testing::Eq;
using ::testing::Optional;

// A filesystem backed by two maps: `paths_` (existence, for Stat / FindRepoRoot's
// `.git` probe) and `contents_` (file bodies, for ReadContent / GlobalExcludesPath's
// config reads). Adding content also marks the path as existing.
class FakeFs : public vfs::FileSystem {
 public:
  void Add(std::string path) { paths_.insert(std::move(path)); }

  void AddContent(const std::string& path, std::string content) {
    paths_.insert(path);
    contents_[path] = std::move(content);
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool /*follow_symlinks*/) const override {
    if (paths_.contains(std::string(path))) {
      return vfs::Metadata{};
    }
    return absl::NotFoundError("FakeFs: no such path");
  }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    const auto it = contents_.find(std::string(path));
    if (it == contents_.end()) {
      return absl::NotFoundError("FakeFs: no such file");
    }
    return it->second;
  }

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view) const override {
    return absl::UnimplementedError("unused");
  }

  absl::Status Remove(std::string_view) const override { return absl::UnimplementedError("unused"); }

  bool Access(std::string_view, vfs::AccessMode) const override { return false; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return absl::UnimplementedError("unused"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return absl::UnimplementedError("unused"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

 private:
  std::set<std::string> paths_;
  std::map<std::string, std::string> contents_;
};

using StatOnlyFs = FakeFs;  // the FindRepoRoot tests below only exercise Stat

struct RepoTest : ::testing::Test {};

TEST_F(RepoTest, FindsGitInTheStartDirectoryItself) {
  StatOnlyFs fs;
  fs.Add("/a/b/.git");
  EXPECT_THAT(FindRepoRoot(fs, "/a/b"), Optional(Eq("/a/b")));
}

TEST_F(RepoTest, WalksUpToAnAncestorRepoRoot) {
  StatOnlyFs fs;
  fs.Add("/a/.git");
  EXPECT_THAT(FindRepoRoot(fs, "/a/b/c"), Optional(Eq("/a")));
}

TEST_F(RepoTest, NearestAncestorWinsWhenNested) {
  StatOnlyFs fs;
  fs.Add("/a/.git");
  fs.Add("/a/b/.git");  // a nested repo (e.g. a submodule): the deeper one owns c
  EXPECT_THAT(FindRepoRoot(fs, "/a/b/c"), Optional(Eq("/a/b")));
}

TEST_F(RepoTest, AGitFilePointerCountsNotJustADirectory) {
  // A worktree/submodule has a `.git` *file*; Stat succeeds for it too, so it counts.
  StatOnlyFs fs;
  fs.Add("/wt/.git");  // StatOnlyFs does not model type: existence is the whole test
  EXPECT_THAT(FindRepoRoot(fs, "/wt/sub"), Optional(Eq("/wt")));
}

TEST_F(RepoTest, FindsARepoAtTheFilesystemRoot) {
  StatOnlyFs fs;
  fs.Add("/.git");
  EXPECT_THAT(FindRepoRoot(fs, "/a/b"), Optional(Eq("/")));
}

TEST_F(RepoTest, ReturnsNulloptWhenNoAncestorHasGit) {
  const StatOnlyFs fs;
  EXPECT_THAT(FindRepoRoot(fs, "/a/b/c"), Eq(std::nullopt));
}

TEST_F(RepoTest, ReturnsNulloptFromTheRootWithNoGit) {
  const StatOnlyFs fs;
  EXPECT_THAT(FindRepoRoot(fs, "/"), Eq(std::nullopt));
}

TEST_F(RepoTest, RelativeStartWithoutASeparatorStopsAfterItsOwnProbe) {
  const StatOnlyFs fs;
  EXPECT_THAT(FindRepoRoot(fs, "relative"), Eq(std::nullopt));
}

TEST_F(RepoTest, GlobalExcludesReadsCoreExcludesFileFromGitconfig) {
  FakeFs fs;
  fs.AddContent("/home/u/.gitconfig", "[core]\n\texcludesfile = /custom/ignore\n");
  EXPECT_THAT(GlobalExcludesPath(fs, {.home = "/home/u"}), Optional(Eq("/custom/ignore")));
}

TEST_F(RepoTest, GlobalExcludesExpandsALeadingTilde) {
  FakeFs fs;
  fs.AddContent("/home/u/.gitconfig", "[core]\nexcludesfile = ~/my.ignore\n");
  EXPECT_THAT(GlobalExcludesPath(fs, {.home = "/home/u"}), Optional(Eq("/home/u/my.ignore")));
}

TEST_F(RepoTest, GlobalExcludesExpandsAnExactTilde) {
  FakeFs fs;
  fs.AddContent("/home/u/.gitconfig", "[core]\nexcludesfile = ~\n");
  EXPECT_THAT(GlobalExcludesPath(fs, {.home = "/home/u"}), Optional(Eq("/home/u")));
}

TEST_F(RepoTest, GlobalExcludesLeavesOrdinaryRelativePathsUnchanged) {
  FakeFs fs;
  fs.AddContent("/home/u/.gitconfig", "[core]\nexcludesfile = relative.ignore\n");
  EXPECT_THAT(GlobalExcludesPath(fs, {.home = "/home/u"}), Optional(Eq("relative.ignore")));
}

TEST_F(RepoTest, GlobalExcludesKeyAndSectionAreCaseInsensitive) {
  FakeFs fs;
  fs.AddContent("/home/u/.gitconfig", "[CORE]\n  ExcludesFile = \"/quoted/path\"\n");  // quotes stripped too
  EXPECT_THAT(GlobalExcludesPath(fs, {.home = "/home/u"}), Optional(Eq("/quoted/path")));
}

TEST_F(RepoTest, GlobalExcludesDefaultsToXdgGitIgnoreWhenUnset) {
  const FakeFs fs;  // no config files at all
  EXPECT_THAT(GlobalExcludesPath(fs, {.home = "/home/u"}), Optional(Eq("/home/u/.config/git/ignore")));
}

TEST_F(RepoTest, GlobalExcludesHonorsXdgConfigHome) {
  FakeFs fs;
  fs.AddContent("/xdg/git/config", "[core]\nexcludesfile = /from/xdg\n");
  EXPECT_THAT(GlobalExcludesPath(fs, {.home = "/home/u", .xdg_config_home = "/xdg"}), Optional(Eq("/from/xdg")));
}

TEST_F(RepoTest, GlobalExcludesGitconfigWinsOverXdg) {
  FakeFs fs;
  fs.AddContent("/xdg/git/config", "[core]\nexcludesfile = /from/xdg\n");
  fs.AddContent("/home/u/.gitconfig", "[core]\nexcludesfile = /from/gitconfig\n");
  EXPECT_THAT(GlobalExcludesPath(fs, {.home = "/home/u", .xdg_config_home = "/xdg"}), Optional(Eq("/from/gitconfig")));
}

TEST_F(RepoTest, GlobalExcludesUsesTheLastCoreValueAndIgnoresOtherIniSyntax) {
  FakeFs fs;
  fs.AddContent(
      "/home/u/.gitconfig",
      "\n# comment\n; another comment\n[other]\nexcludesfile = /wrong-section\n"
      "[broken\nexcludesfile = /wrong-malformed-section\n[ core ]\n"
      "missing-equals\nwrong-key = /wrong-key\nexcludesfile = /first\nexcludesfile = \"/last\"\n");
  EXPECT_THAT(GlobalExcludesPath(fs, {.home = "/home/u"}), Optional(Eq("/last")));
}

TEST_F(RepoTest, GlobalExcludesCanComeFromXdgWithoutAHomeDirectory) {
  FakeFs fs;
  fs.AddContent("/xdg/git/config", "[core]\nexcludesfile = ~/literal-without-home\n");
  EXPECT_THAT(GlobalExcludesPath(fs, {.xdg_config_home = "/xdg"}), Optional(Eq("~/literal-without-home")));
}

TEST_F(RepoTest, GlobalExcludesDefaultsToXdgWithoutAHomeDirectory) {
  const FakeFs fs;
  EXPECT_THAT(GlobalExcludesPath(fs, {.xdg_config_home = "/xdg"}), Optional(Eq("/xdg/git/ignore")));
}

TEST_F(RepoTest, GlobalExcludesIsNulloptWithoutHomeOrXdg) {
  const FakeFs fs;
  EXPECT_THAT(GlobalExcludesPath(fs, {}), Eq(std::nullopt));
}

}  // namespace
}  // namespace xff::repo
