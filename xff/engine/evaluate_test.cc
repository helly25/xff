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

#include "xff/engine/evaluate.h"

#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "mbo/testing/status.h"
#include "xff/engine/walk.h"
#include "xff/parser/parser.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/local_fs.h"

namespace xff::engine {
namespace {

using ::mbo::testing::IsOk;
using ::testing::Eq;
using ::testing::IsEmpty;

struct EvaluateTest : ::testing::Test {
  // Parses `. <expr...>` and evaluates the expression against `visit`, capturing
  // any action output in `emitted_`.
  bool Match(const std::vector<std::string>& expr, const Visit& visit) {
    emitted_.clear();
    std::vector<std::string> argv;
    argv.reserve(expr.size() + 1);
    argv.emplace_back(".");
    argv.insert(argv.end(), expr.begin(), expr.end());
    const auto command = parser::Parse(argv);
    EXPECT_THAT(command, IsOk());
    if (!command.ok() || command->expression == nullptr) {
      return false;
    }
    return Evaluate(*command->expression, visit, [this](std::string_view record) { emitted_ += record; }, fs_);
  }

  // A Visit of `type`, with `path`/`name` backed by the caller and metadata by
  // `storage` (both must outlive the returned Visit).
  static Visit MakeVisit(std::string_view path, std::string_view name, vfs::FileType type, vfs::Metadata& storage) {
    storage.type = type;
    return Visit{.path = path, .name = name, .depth = 1, .metadata = storage};
  }

  std::string emitted_;
  vfs::LocalFs fs_;
};

TEST_F(EvaluateTest, TrueAndFalse) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo", "foo", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-true"}, visit));
  EXPECT_FALSE(Match({"-false"}, visit));
}

TEST_F(EvaluateTest, NameGlobsBasename) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-name", "*.txt"}, visit));
  EXPECT_TRUE(Match({"-name", "foo.*"}, visit));
  EXPECT_FALSE(Match({"-name", "*.md"}, visit));
  EXPECT_FALSE(Match({"-name", "dir/*"}, visit)) << "-name matches the basename, not the path";
}

TEST_F(EvaluateTest, INameFoldsCase) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/Foo.TXT", "Foo.TXT", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-iname", "foo.txt"}, visit));
  EXPECT_FALSE(Match({"-name", "foo.txt"}, visit));
}

TEST_F(EvaluateTest, PathGlobsWholePath) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/c.txt", "c.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-path", "a/*/c.txt"}, visit));
  EXPECT_TRUE(Match({"-path", "*/c.txt"}, visit)) << "* spans slashes for -path";
  EXPECT_FALSE(Match({"-path", "c.txt"}, visit));
  EXPECT_TRUE(Match({"-ipath", "A/B/*"}, visit));
}

TEST_F(EvaluateTest, TypeMatchesFileType) {
  vfs::Metadata file_md;
  const Visit file = MakeVisit("x", "x", vfs::FileType::kRegular, file_md);
  vfs::Metadata dir_md;
  const Visit dir = MakeVisit("d", "d", vfs::FileType::kDirectory, dir_md);
  EXPECT_TRUE(Match({"-type", "f"}, file));
  EXPECT_FALSE(Match({"-type", "d"}, file));
  EXPECT_TRUE(Match({"-type", "d"}, dir));
  EXPECT_FALSE(Match({"-type", "f"}, dir));
}

TEST_F(EvaluateTest, AndOrNotShortCircuit) {
  vfs::Metadata md;
  const Visit txt = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-type", "f", "-name", "*.txt"}, txt));        // implicit -a
  EXPECT_FALSE(Match({"-type", "d", "-name", "*.txt"}, txt));       // -a, first fails
  EXPECT_TRUE(Match({"-type", "d", "-o", "-name", "*.txt"}, txt));  // -o
  EXPECT_TRUE(Match({"!", "-type", "d"}, txt));                     // -not
  EXPECT_FALSE(Match({"!", "-name", "*.txt"}, txt));
}

TEST_F(EvaluateTest, PrintActionsEmit) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-print"}, visit));
  EXPECT_THAT(emitted_, Eq("dir/foo.txt\n"));
  EXPECT_TRUE(Match({"-print0"}, visit));
  EXPECT_THAT(emitted_, Eq(std::string("dir/foo.txt\0", 12)));
}

TEST_F(EvaluateTest, ShortCircuitSkipsAction) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-type", "f", "-print"}, visit));
  EXPECT_THAT(emitted_, Eq("dir/foo.txt\n"));
  EXPECT_FALSE(Match({"-type", "d", "-print"}, visit)) << "-type d fails; -a short-circuits before -print";
  EXPECT_THAT(emitted_, IsEmpty());
}

TEST_F(EvaluateTest, SizeMatchesBytesAndUnits) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.size = 5;  // bytes
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-size", "5c"}, visit));
  EXPECT_FALSE(Match({"-size", "4c"}, visit));
  EXPECT_TRUE(Match({"-size", "+4c"}, visit));
  EXPECT_TRUE(Match({"-size", "-6c"}, visit));
  EXPECT_FALSE(Match({"-size", "+5c"}, visit));
  EXPECT_TRUE(Match({"-size", "1"}, visit)) << "5 bytes rounds up to one 512-byte block";
  EXPECT_TRUE(Match({"-size", "1k"}, visit)) << "5 bytes rounds up to one 1k unit";
}

TEST_F(EvaluateTest, PermMatchesOctalModes) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mode = 0644;  // rw-r--r--
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-perm", "644"}, visit));    // exact
  EXPECT_FALSE(Match({"-perm", "640"}, visit));
  EXPECT_TRUE(Match({"-perm", "-200"}, visit));   // -MODE: owner-write bit set
  EXPECT_TRUE(Match({"-perm", "-044"}, visit));   // group + other read set
  EXPECT_FALSE(Match({"-perm", "-022"}, visit));  // group/other write NOT set
  EXPECT_TRUE(Match({"-perm", "/040"}, visit));   // /MODE: any bit (group read) set
  EXPECT_FALSE(Match({"-perm", "/022"}, visit));  // none of group/other write set
}

TEST_F(EvaluateTest, EmptyMatchesZeroByteFilesNotOthers) {
  vfs::Metadata empty_file;
  empty_file.type = vfs::FileType::kRegular;
  empty_file.size = 0;
  EXPECT_TRUE(Match({"-empty"}, Visit{.path = "f", .name = "f", .depth = 1, .metadata = empty_file}));

  vfs::Metadata nonempty_file;
  nonempty_file.type = vfs::FileType::kRegular;
  nonempty_file.size = 5;
  EXPECT_FALSE(Match({"-empty"}, Visit{.path = "f", .name = "f", .depth = 1, .metadata = nonempty_file}));

  vfs::Metadata link;
  link.type = vfs::FileType::kSymlink;
  EXPECT_FALSE(Match({"-empty"}, Visit{.path = "l", .name = "l", .depth = 1, .metadata = link}))
      << "-empty matches only regular files and directories";
}

TEST_F(EvaluateTest, LinksMatchesHardLinkCount) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.nlink = 1;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-links", "1"}, visit));
  EXPECT_FALSE(Match({"-links", "2"}, visit));
  EXPECT_TRUE(Match({"-links", "-2"}, visit));
  EXPECT_TRUE(Match({"-links", "+0"}, visit));
  EXPECT_FALSE(Match({"-links", "+1"}, visit));
}

TEST_F(EvaluateTest, NewerFalseWhenReferenceMissing) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // The reference cannot be stat'd, so -newer is false (real comparisons are
  // covered by the conformance test against actual files).
  EXPECT_FALSE(Match({"-newer", "/no/such/reference/file"}, visit));
}

}  // namespace
}  // namespace xff::engine
