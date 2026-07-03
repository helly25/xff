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

#include "xff/repo/repo.h"

#include <set>
#include <string>
#include <string_view>

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

// A filesystem whose only meaningful query is Stat: a path exists iff it was added.
// FindRepoRoot only probes `<dir>/.git` via Stat, so nothing else needs to work.
class StatOnlyFs : public vfs::FileSystem {
 public:
  void Add(std::string path) { paths_.insert(std::move(path)); }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool /*follow_symlinks*/) const override {
    if (paths_.contains(std::string(path))) {
      return vfs::Metadata{};
    }
    return absl::NotFoundError("StatOnlyFs: no such path");
  }

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view) const override {
    return absl::UnimplementedError("unused");
  }

  absl::Status Remove(std::string_view) const override { return absl::UnimplementedError("unused"); }

  bool Access(std::string_view, vfs::AccessMode) const override { return false; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return absl::UnimplementedError("unused"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return absl::UnimplementedError("unused"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override {
    return absl::UnimplementedError("unused");
  }

 private:
  std::set<std::string> paths_;
};

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
  StatOnlyFs fs;
  EXPECT_THAT(FindRepoRoot(fs, "/a/b/c"), Eq(std::nullopt));
}

TEST_F(RepoTest, ReturnsNulloptFromTheRootWithNoGit) {
  StatOnlyFs fs;
  EXPECT_THAT(FindRepoRoot(fs, "/"), Eq(std::nullopt));
}

}  // namespace
}  // namespace xff::repo
