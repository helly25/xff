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
        [](std::string_view, absl::Status) {});
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
      [](std::string_view, absl::Status) {});
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

TEST_F(RunTest, DepthVisitsPostOrder) {
  const std::vector<std::string> out = RunExpr({"-depth"});
  // -depth lists the same set but post-order, so the root operand prints last.
  EXPECT_THAT(
      out, UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  ASSERT_FALSE(out.empty());
  EXPECT_THAT(out.back(), Eq(root_.string()));
}

TEST_F(RunTest, SymlinkLModeFollowsDirectorySymlink) {
  std::error_code ec;
  fs::create_directory_symlink(root_ / "sub", root_ / "lnk", ec);
  ASSERT_FALSE(ec);
  // `find -L <root> -name c.txt`: -L follows the directory symlink lnk -> sub, so
  // c.txt is reachable both directly (sub/c.txt) and through the link (lnk/c.txt).
  const auto command = parser::Parse({"-L", root_.string(), "-name", "c.txt"});
  ASSERT_THAT(command, IsOk());
  std::vector<std::string> out;
  RunFind(
      *command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && (text.back() == '\n' || text.back() == '\0')) {
          text.pop_back();
        }
        out.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(out, UnorderedElementsAre(Path("sub/c.txt"), Path("lnk/c.txt")));
}

TEST_F(RunTest, FormatJsonlRendersImplicitPrintAsJson) {
  const auto command = parser::Parse({"--format=jsonl", root_.string(), "-name", "a.txt"});
  ASSERT_THAT(command, IsOk());
  std::vector<std::string> records;
  RunFind(
      *command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre(Eq(std::string("{\"path\":\"") + Path("a.txt") + "\"}\n")));
}

TEST_F(RunTest, FormatNulViaDashZero) {
  const auto command = parser::Parse({"-0", root_.string(), "-name", "a.txt"});
  ASSERT_THAT(command, IsOk());
  std::vector<std::string> records;
  RunFind(
      *command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre(Eq(Path("a.txt") + std::string("\0", 1))));
}

TEST_F(RunTest, DeleteRemovesMatchedFiles) {
  RunExpr({"-name", "*.txt", "-delete"});  // -delete implies -depth, so children go first
  EXPECT_FALSE(fs::exists(root_ / "a.txt"));
  EXPECT_FALSE(fs::exists(root_ / "sub" / "c.txt"));
  EXPECT_TRUE(fs::exists(root_ / "b.md"));  // not matched
}

TEST_F(RunTest, DeleteDryRunPreviewsWithoutDeleting) {
  const auto command = parser::Parse({"--dry-run", root_.string(), "-name", "a.txt", "-delete"});
  ASSERT_THAT(command, IsOk());
  std::vector<std::string> records;
  RunFind(
      *command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_TRUE(fs::exists(root_ / "a.txt"));  // --dry-run: nothing deleted
  EXPECT_THAT(records, UnorderedElementsAre(Path("a.txt")));  // but previewed
}

TEST_F(RunTest, SafeRefusesDelete) {
  const auto command = parser::Parse({"--safe", root_.string(), "-delete"});
  ASSERT_THAT(command, IsOk());
  const int errors =
      RunFind(*command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
  EXPECT_THAT(errors, Eq(2));
  EXPECT_TRUE(fs::exists(root_ / "a.txt"));  // refused: nothing deleted
}

TEST_F(RunTest, ExecRunsCommandPerMatch) {
  // -exec /bin/sh -c 'echo > "{}.ran"' ; creates a marker beside each matched file.
  RunExpr({"-name", "a.txt", "-exec", "/bin/sh", "-c", "echo > \"{}.ran\"", ";"});
  EXPECT_TRUE(fs::exists(root_ / "a.txt.ran"));
}

TEST_F(RunTest, SafeRefusesExec) {
  const auto command =
      parser::Parse({"--safe", root_.string(), "-exec", "/bin/sh", "-c", "echo > \"{}.ran\"", ";"});
  ASSERT_THAT(command, IsOk());
  const int errors =
      RunFind(*command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
  EXPECT_THAT(errors, Eq(2));
  EXPECT_FALSE(fs::exists(root_ / "a.txt.ran"));  // refused: command not run
}

TEST_F(RunTest, TemplateRendersImplicitPrint) {
  const auto command = parser::Parse({"--template={name}:{type}", root_.string(), "-name", "a.txt"});
  ASSERT_THAT(command, IsOk());
  std::vector<std::string> records;
  RunFind(
      *command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre("a.txt:f"));  // {name}:{type} for a regular file
}

TEST_F(RunTest, TemplateRootFieldReportsTheSearchOperand) {
  // {root} is the command-line operand a match descends from (find %H); a nested
  // match (sub/c.txt) still reports the operand, exercising run.cc's wiring of
  // Visit::root into the render context.
  const auto command = parser::Parse({"--template={root}|{name}", root_.string(), "-name", "c.txt"});
  ASSERT_THAT(command, IsOk());
  std::vector<std::string> records;
  RunFind(
      *command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre(root_.string() + "|c.txt"));
}

TEST_F(RunTest, ExecFieldsRendersNamedPlaceholders) {
  // --exec-fields routes -exec tokens through the field vocabulary: {path} is the
  // full path, so the marker lands beside the matched file (vs. a literal "{path}"
  // file in the cwd without the flag).
  const auto command = parser::Parse(
      {"--exec-fields", root_.string(), "-name", "a.txt", "-exec", "/bin/sh", "-c", "echo > \"{path}.fld\"", ";"});
  ASSERT_THAT(command, IsOk());
  RunFind(*command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
  EXPECT_TRUE(fs::exists(root_ / "a.txt.fld"));
}

TEST_F(RunTest, ExecFieldsSubstitutesRegexCaptures) {
  // --exec-fields + a -regex match: {1}/{2} resolve to the capture groups, written
  // to a marker beside the file ({path} keeps the marker absolute for cleanup).
  const auto command = parser::Parse(
      {"--exec-fields", root_.string(), "-regex", ".*/(a)\\.(txt)", "-exec", "/bin/sh", "-c",
       "printf '%s' \"{1}.{2}\" > \"{path}.cap\"", ";"});
  ASSERT_THAT(command, IsOk());
  RunFind(*command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
  const fs::path marker = root_ / "a.txt.cap";
  ASSERT_TRUE(fs::exists(marker));
  std::ifstream in(marker);
  std::string content;
  std::getline(in, content);
  EXPECT_THAT(content, Eq("a.txt"));  // {1}="a", {2}="txt"
}

TEST_F(RunTest, DefinePopulatesDefNamespace) {
  // --define=NAME=VALUE surfaces as {def.NAME} in --template output (last wins).
  const auto command = parser::Parse(
      {"--define=label=old", "--define=label=new", "--template={def.label}:{name}", root_.string(), "-name",
       "a.txt"});
  ASSERT_THAT(command, IsOk());
  std::vector<std::string> records;
  RunFind(
      *command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre("new:a.txt"));  // last --define wins
}

TEST_F(RunTest, CaptureBindsOutputForTemplate) {
  // --capture runs a command per match (with {} -> path) and binds its stdout to
  // {output.NAME}; --template then prints it.
  const auto command = parser::Parse(
      {"--template={output.base}", root_.string(), "-name", "a.txt", "--capture=base", "/bin/sh", "-c",
       "basename {}", ";"});
  ASSERT_THAT(command, IsOk());
  std::vector<std::string> records;
  RunFind(
      *command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre("a.txt"));
}

TEST_F(RunTest, CaptureChainsPriorOutputs) {
  // A later --capture command references an earlier capture's {output.*}.
  const auto command = parser::Parse(
      {"--template={output.b}", root_.string(), "-name", "a.txt", "--capture=a", "/bin/sh", "-c", "printf X", ";",
       "--capture=b", "/bin/sh", "-c", "printf {output.a}Y", ";"});
  ASSERT_THAT(command, IsOk());
  std::vector<std::string> records;
  RunFind(
      *command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre("XY"));  // b = {output.a}("X") + "Y"
}

}  // namespace
}  // namespace xff::engine
