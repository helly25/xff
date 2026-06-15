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

#include "xff/engine/run.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "absl/status/status.h"
#include "mbo/testing/status.h"
#include "xff/parser/parser.h"
#include "xff/vfs/local_fs.h"

namespace xff::engine {
namespace {

namespace fs = std::filesystem;

using ::mbo::testing::IsOk;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

// Fixture tree:
//   <root>/a.txt
//   <root>/b.md
//   <root>/sub/c.txt
struct RunTest : ::testing::Test {
  void SetUp() override {
    root_ = fs::path(::testing::TempDir()) /
            (std::string("xff_run_") + ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::error_code ec;
    fs::remove_all(root_, ec);
    ASSERT_TRUE(fs::create_directories(root_ / "sub"));
    { std::ofstream(root_ / "a.txt") << "a"; }
    { std::ofstream(root_ / "b.md") << "b"; }
    { std::ofstream(root_ / "sub" / "c.txt") << "c"; }
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }

  std::string Path(std::string_view child) const { return (root_ / child).string(); }

  // Parses `<root> <expr...>`, runs it, and returns the emitted records with a
  // single trailing terminator ('\n' or '\0') stripped.
  std::vector<std::string> RunExpr(const std::vector<std::string>& expr) {
    std::vector<std::string> argv = {root_.string()};
    argv.insert(argv.end(), expr.begin(), expr.end());
    const auto command = parser::Parse(argv);
    EXPECT_THAT(command, IsOk());
    std::vector<std::string> records;
    last_errors_ = RunFind(
        *command, fs_,
        [&](std::string_view record) {
          std::string text(record);
          if (!text.empty() && (text.back() == '\n' || text.back() == '\0')) {
            text.pop_back();
          }
          records.push_back(std::move(text));
        },
        [](std::string_view, const absl::Status&) {});
    return records;
  }

  vfs::LocalFs fs_;
  fs::path root_;
  int last_errors_ = 0;
};

TEST_F(RunTest, NoExpressionPrintsEverything) {
  EXPECT_THAT(
      RunExpr({}),
      UnorderedElementsAre(
          root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, Eq(0));
}

TEST_F(RunTest, NameGlobImplicitPrint) {
  EXPECT_THAT(RunExpr({"-name", "*.txt"}), UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
}

TEST_F(RunTest, TypeDirectoryImplicitPrint) {
  EXPECT_THAT(RunExpr({"-type", "d"}), UnorderedElementsAre(root_.string(), Path("sub")));
}

TEST_F(RunTest, ExplicitPrintIsNotDoubled) {
  EXPECT_THAT(RunExpr({"-name", "*.txt", "-print"}), UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
}

TEST_F(RunTest, Print0EmitsNulTerminatedRecords) {
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-print0"}), UnorderedElementsAre(Path("a.txt")));
}

TEST_F(RunTest, MaxDepthLimitsDescent) {
  // -maxdepth 1: root + its direct children, but not sub/c.txt (depth 2).
  EXPECT_THAT(
      RunExpr({"-maxdepth", "1"}),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub")));
}

TEST_F(RunTest, MinDepthSkipsRoot) {
  // -mindepth 1: everything except the root operand itself.
  EXPECT_THAT(
      RunExpr({"-mindepth", "1"}),
      UnorderedElementsAre(Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
}

TEST_F(RunTest, EmptyMatchesEmptyFileAndDir) {
  std::error_code ec;
  { std::ofstream(root_ / "empty.txt"); }  // 0 bytes
  fs::create_directory(root_ / "emptydir", ec);
  // -empty: the zero-byte file and the childless directory only (a.txt/b.md/
  // sub/c.txt are non-empty; root and sub have children).
  EXPECT_THAT(RunExpr({"-empty"}), UnorderedElementsAre(Path("empty.txt"), Path("emptydir")));
}

TEST_F(RunTest, LinksOneMatchesRegularFiles) {
  // Regular files have one hard link; directories have >= 2.
  EXPECT_THAT(
      RunExpr({"-links", "1"}),
      UnorderedElementsAre(Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
}

TEST_F(RunTest, MissingRootCountsError) {
  std::vector<std::string> argv = {(root_ / "absent").string(), "-print"};
  const auto command = parser::Parse(argv);
  ASSERT_THAT(command, IsOk());
  std::vector<std::string> records;
  const int errors = RunFind(
      *command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [](std::string_view, const absl::Status&) {});
  EXPECT_THAT(records, IsEmpty());
  EXPECT_THAT(errors, Eq(1));
}

TEST_F(RunTest, PruneSkipsDirectoryDescent) {
  // `-name sub -prune -o -print`: prints everything except `sub` and its contents.
  EXPECT_THAT(
      RunExpr({"-name", "sub", "-prune", "-o", "-print"}),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md")));
}

TEST_F(RunTest, QuitStopsTraversal) {
  // `-quit` is an action (so no implicit -print) that stops after the first entry.
  EXPECT_THAT(RunExpr({"-quit"}), IsEmpty());
}

}  // namespace
}  // namespace xff::engine
