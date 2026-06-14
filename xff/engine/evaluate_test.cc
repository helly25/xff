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

namespace xff::engine {
namespace {

using ::mbo::testing::IsOk;

struct EvaluateTest : ::testing::Test {
  // Parses `. <expr...>` and evaluates the resulting expression against `visit`.
  bool Match(const std::vector<std::string>& expr, const Visit& visit) {
    std::vector<std::string> argv;
    argv.reserve(expr.size() + 1);
    argv.emplace_back(".");
    argv.insert(argv.end(), expr.begin(), expr.end());
    const auto command = parser::Parse(argv);
    EXPECT_THAT(command, IsOk());
    if (!command.ok() || command->expression == nullptr) {
      return false;
    }
    return Evaluate(*command->expression, visit);
  }

  // A Visit of `type`, with `path`/`name` backed by the caller and metadata by
  // `storage` (both must outlive the returned Visit).
  static Visit MakeVisit(std::string_view path, std::string_view name, vfs::FileType type, vfs::Metadata& storage) {
    storage.type = type;
    return Visit{.path = path, .name = name, .depth = 1, .metadata = storage};
  }
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

TEST_F(EvaluateTest, ActionsEvaluateTrue) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-print"}, visit));
  EXPECT_TRUE(Match({"-type", "f", "-print"}, visit));
  EXPECT_FALSE(Match({"-type", "d", "-print"}, visit)) << "-type d fails; -a short-circuits before -print";
}

}  // namespace
}  // namespace xff::engine
