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

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/parser/parser.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"
#include "xff/vfs/local_fs.h"

namespace xff::engine {
namespace {

namespace fs = std::filesystem;

using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::UnorderedElementsAre;

// Fixture tree:
//   <root>/a.txt
//   <root>/b.md
//   <root>/sub/c.txt
struct RunTest : ::testing::Test {
  void SetUp() override {
    root_ = fs::path(::testing::TempDir())
            / (std::string("xff_run_") + ::testing::UnitTest::GetInstance()->current_test_info()->name());
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

  // Like RunExpr, but takes the whole argv (so leading globals such as --summary
  // can come before the root), returning the emitted records, terminator stripped.
  std::vector<std::string> RunArgvRecords(const std::vector<std::string>& argv) {
    const auto command = parser::Parse(argv);
    EXPECT_THAT(command, IsOk());
    std::vector<std::string> records;
    if (!command.ok()) {
      return records;
    }
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

  // Runs the bare root under `style` to exercise the mode-scoped traversal
  // defaults (RunFind's `style`), returning records with the terminator stripped.
  std::vector<std::string> RunStyled(registry::Style style) {
    const auto command = parser::Parse({root_.string()});
    EXPECT_THAT(command, IsOk());
    std::vector<std::string> records;
    if (!command.ok()) {
      return records;
    }
    RunFind(
        *command, fs_,
        [&](std::string_view record) {
          std::string text(record);
          if (!text.empty() && (text.back() == '\n' || text.back() == '\0')) {
            text.pop_back();
          }
          records.push_back(std::move(text));
        },
        [](std::string_view, absl::Status) {}, style);
    return records;
  }

  vfs::LocalFs fs_;
  fs::path root_;
  int last_errors_ = 0;
};

TEST_F(RunTest, NoExpressionPrintsEverything) {
  EXPECT_THAT(
      RunExpr({}), UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, FprintWritesMatchesToFileNotStdout) {
  // -fprint FILE redirects the matched paths into FILE; being an action, it also
  // suppresses the implicit -print, so stdout stays empty. The output file lives
  // outside the walked tree so it never appears in its own results.
  const std::string out = (fs::path(::testing::TempDir()) / "xff_fprint_out.lst").string();
  std::error_code ec;
  fs::remove(out, ec);
  EXPECT_THAT(RunExpr({"-name", "*.txt", "-fprint", out}), IsEmpty());
  EXPECT_THAT(last_errors_, 0);
  std::ifstream in(out, std::ios::binary);
  ASSERT_TRUE(in.good());
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    lines.push_back(line);
  }
  EXPECT_THAT(lines, UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
  fs::remove(out, ec);
}

TEST_F(RunTest, FprintlnAndFprintflnWriteWithOsLineEndingToFile) {
  // xff: the file-writing forms of -println / -printfln, the counterparts of -fprint /
  // -fprintf. Each redirects into FILE (so, being an action, the implicit -print is
  // suppressed and stdout stays empty) and terminates the record with the OS line
  // ending (here "\n"). -fprintfln takes FILE then FORMAT, like -fprintf.
  const auto read_all = [](const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  };
  const std::string ln = (fs::path(::testing::TempDir()) / "xff_fprintln.out").string();
  const std::string fln = (fs::path(::testing::TempDir()) / "xff_fprintfln.out").string();
  std::error_code ec;
  fs::remove(ln, ec);
  fs::remove(fln, ec);

  EXPECT_THAT(RunExpr({"-name", "a.txt", "-fprintln", ln}), IsEmpty());
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-fprintfln", fln, "name %f"}), IsEmpty());
  EXPECT_THAT(last_errors_, 0);

  EXPECT_THAT(read_all(ln), Eq(Path("a.txt") + "\n"));
  EXPECT_THAT(read_all(fln), Eq(std::string("name a.txt\n")));

  fs::remove(ln, ec);
  fs::remove(fln, ec);
}

TEST_F(RunTest, PrintfPercentBraceEscapeExpandsXffFields) {
  // xff: `%{field}` in a -printf format reaches the brace field vocabulary (here
  // {relpath}); `%%` stays a literal percent, a bare `{..}` stays literal (printf formats
  // legitimately contain braces), and an unterminated `%{` is emitted literally. The whole
  // format renders as one record (it owns its terminator).
  EXPECT_THAT(
      RunExpr({"-name", "a.txt", "-printf", "rel=%{relpath} f=%f pct=%% bare={x} bad=%{oops\n"}),
      ElementsAre("rel=a.txt f=a.txt pct=% bare={x} bad=%{oops"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, DaystartFeedsTheTimeTests) {
  // Age a.txt to ~10 days ago, then select with -daystart -mtime +5 (older than
  // ~5 days, measured from today's local midnight). 10 days clears the boundary
  // with room to spare, so this exercises the daystart -> reference-instant ->
  // time-test wiring end to end without depending on the exact midnight cutoff.
  const auto ten_days_ago = fs::file_time_type::clock::now() - std::chrono::hours(24 * 10);
  std::error_code ec;
  fs::last_write_time(root_ / "a.txt", ten_days_ago, ec);
  ASSERT_FALSE(ec);
  EXPECT_THAT(RunExpr({"-daystart", "-mtime", "+5", "-name", "a.txt"}), ElementsAre(Path("a.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, TraversalSynonymsAccepted) {
  // -mount/-x (= -xdev) and -d (= -depth) are accepted; on a single-device tree
  // -xdev prunes nothing, and -d only reorders (post-order), so the set is the same.
  EXPECT_THAT(
      RunExpr({"-mount"}),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(
      RunExpr({"-x"}),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(RunExpr({"-d", "-name", "*.txt"}), UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, IgnoreReaddirRaceAccepted) {
  // The option parses and walks normally (no races on this stable tree); the
  // ENOENT-suppression behaviour itself is covered at the walk level.
  EXPECT_THAT(
      RunExpr({"-ignore_readdir_race"}),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(
      RunExpr({"-noignore_readdir_race", "-name", "*.txt"}), UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ExecPlusBatchesAllMatchesIntoOneRun) {
  // Engine-level: RunFind accumulates the matches and flushes ONE batched command
  // at end-of-walk (the exec_batches map + post-walk flush), in-process. The shell
  // appends a RUN marker per invocation plus each path, so exactly one RUN line
  // proves a single batched run (per-entry would yield two). The full binary/CLI
  // path is covered at the system level in //xff/cli:exec_test (helly25/bashtest).
  const std::string out = (fs::path(::testing::TempDir()) / "xff_execplus_out.lst").string();
  std::error_code ec;
  fs::remove(out, ec);
  const std::string script = "echo RUN >> '" + out + "'; for p in \"$@\"; do echo \"$p\" >> '" + out + "'; done";
  RunExpr({"-name", "*.txt", "-exec", "sh", "-c", script, "_", "{}", "+"});
  EXPECT_THAT(last_errors_, 0);
  std::ifstream in(out, std::ios::binary);
  ASSERT_TRUE(in.good());
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    lines.push_back(line);
  }
  EXPECT_THAT(lines, UnorderedElementsAre("RUN", Path("a.txt"), Path("sub/c.txt")));
  fs::remove(out, ec);
}

TEST_F(RunTest, ExecdirPlusBatchesPerDirectory) {
  // *.txt are in two directories (root/a.txt, root/sub/c.txt), so -execdir ... +
  // runs ONCE PER DIRECTORY, passing the ./basename. Two dirs -> two RUN markers;
  // the items are basenames, not full paths (the cwd is each entry's directory).
  const std::string out = (fs::path(::testing::TempDir()) / "xff_execdirplus_out.lst").string();
  std::error_code ec;
  fs::remove(out, ec);
  const std::string script = "echo RUN >> '" + out + "'; for p in \"$@\"; do echo \"$p\" >> '" + out + "'; done";
  RunExpr({"-name", "*.txt", "-execdir", "sh", "-c", script, "_", "{}", "+"});
  EXPECT_THAT(last_errors_, 0);
  std::ifstream in(out, std::ios::binary);
  ASSERT_TRUE(in.good());
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    lines.push_back(line);
  }
  EXPECT_THAT(lines, UnorderedElementsAre("RUN", "RUN", "./a.txt", "./c.txt"));
  fs::remove(out, ec);
}

TEST_F(RunTest, ExecSemicolonUnderParallelJobsRunsEveryMatch) {
  // -j>1 routes the serial `-exec ... ;` action through the bounded ParallelExec
  // runner. Each of the two *.txt matches must still run exactly once; the children
  // append their path (one short, O_APPEND-atomic line apiece) so order is
  // unspecified but the set is complete -- proving no match is dropped on launch.
  const std::string out = (fs::path(::testing::TempDir()) / "xff_execpar_out.lst").string();
  std::error_code ec;
  fs::remove(out, ec);
  const std::string script = "echo \"$1\" >> '" + out + "'";
  RunArgvRecords({"-j2", root_.string(), "-name", "*.txt", "-exec", "sh", "-c", script, "_", "{}", ";"});
  EXPECT_THAT(last_errors_, 0);
  std::ifstream in(out, std::ios::binary);
  ASSERT_TRUE(in.good());
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    lines.push_back(line);
  }
  EXPECT_THAT(lines, UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
  fs::remove(out, ec);
}

TEST_F(RunTest, ExecSemicolonUnderParallelJobsLeavesExitStatusUnaffected) {
  // find's `-exec ... ;` is a predicate: a nonzero exit makes the action false but
  // does NOT raise find's exit status (unlike the `+` batch form). The parallel
  // runner preserves that -- both *.txt matches run `sh -c 'exit 1'`, yet the run
  // reports no error, identical to the synchronous -j1 path.
  RunArgvRecords({"-j2", root_.string(), "-name", "*.txt", "-exec", "sh", "-c", "exit 1", ";"});
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, JobsAllParsesAndWalksEverything) {
  // --jobs=all resolves to every detected core; the parallel walk still visits the
  // whole tree. The set is complete (order unspecified). On a 1-core host it folds
  // to -j1, which returns the same set, so the assertion holds regardless.
  EXPECT_THAT(
      RunArgvRecords({"--jobs=all", root_.string()}),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ModeScopedSortDefault) {
  // With no --sort, the active style picks the default: modern (kXff) sorts each
  // directory's listing, so the walk is deterministic (root, then a.txt < b.md <
  // sub as a block, then sub's contents). find leaves it unordered (same set).
  EXPECT_THAT(
      RunStyled(registry::Style::kXff),
      ElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(
      RunStyled(registry::Style::kFind),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
}

TEST_F(RunTest, SortNameVisitsSiblingsInDeterministicOrder) {
  // --sort=name orders each directory's entries by name, so the whole walk is
  // deterministic: root first, then a.txt < b.md < sub, then sub/c.txt. ElementsAre
  // (not UnorderedElementsAre) asserts the exact sequence.
  const auto command = parser::Parse({"--sort", root_.string()});
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
  EXPECT_THAT(records, ElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
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
      RunExpr({"-maxdepth", "1"}), UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub")));
}

TEST_F(RunTest, MinDepthSkipsRoot) {
  // -mindepth 1: everything except the root operand itself.
  EXPECT_THAT(
      RunExpr({"-mindepth", "1"}), UnorderedElementsAre(Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
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
  EXPECT_THAT(RunExpr({"-links", "1"}), UnorderedElementsAre(Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
}

TEST_F(RunTest, MimeMatchesByExtensionDerivedType) {
  // Fixture: a.txt + sub/c.txt (text/plain) and b.md (text/markdown) are all text/*;
  // the directories (no extension -> octet-stream) are excluded.
  EXPECT_THAT(RunExpr({"-mime", "text/*"}), UnorderedElementsAre(Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
}

TEST_F(RunTest, MissingRootCountsError) {
  const std::vector<std::string> argv = {(root_ / "absent").string(), "-print"};
  const auto command = parser::Parse(argv);
  ASSERT_THAT(command, IsOk());
  std::vector<std::string> records;
  const int errors = RunFind(
      *command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, IsEmpty());
  EXPECT_THAT(errors, 1);
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
  EXPECT_THAT(out, UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  ASSERT_THAT(out, Not(IsEmpty()));
  EXPECT_THAT(out.back(), root_.string());
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

TEST_F(RunTest, ColorAlwaysWrapsDirectoriesButLeavesPlainFilesUncolored) {
  // --color=always forces ANSI even though the test's captured stdout is a pipe,
  // not a tty (so the default auto would stay plain). A directory gets bold blue
  // (1;34); a plain non-executable regular file is emitted with no escapes.
  EXPECT_THAT(
      RunArgvRecords({"--color=always", root_.string(), "-name", "sub"}),
      ElementsAre(absl::StrCat("\x1b[1;34m", Path("sub"), "\x1b[0m")));
  EXPECT_THAT(RunArgvRecords({"--color=always", root_.string(), "-name", "a.txt"}), ElementsAre(Path("a.txt")));
}

TEST_F(RunTest, TemplateRelpathIsRelativeToTheSearchRoot) {
  // {relpath} renders each entry's path relative to the search root (find %P), so the
  // walk's per-entry root wiring is exercised end-to-end.
  EXPECT_THAT(
      RunArgvRecords({"--template={relpath}", root_.string(), "-type", "f"}),
      UnorderedElementsAre("a.txt", "b.md", "sub/c.txt"));
}

TEST_F(RunTest, TemplateTargetRendersTheSymlinkTarget) {
  // {target} = the symlink's target (find %l), resolved via ReadLink at the render
  // context; empty for a non-symlink. Exercises the engine's link-target wiring e2e.
  std::error_code ec;
  fs::create_symlink("a.txt", root_ / "link.lnk", ec);
  ASSERT_FALSE(ec);
  EXPECT_THAT(RunArgvRecords({"--template={target}", root_.string(), "-name", "link.lnk"}), ElementsAre("a.txt"));
  EXPECT_THAT(RunArgvRecords({"--template=[{target}]", root_.string(), "-name", "a.txt"}), ElementsAre("[]"));
}

TEST_F(RunTest, CmpMatchesByteIdenticalContent) {
  { std::ofstream(root_ / "twin.txt") << "a"; }  // byte-identical to a.txt (content "a")
  { std::ofstream(root_ / "diff.txt") << "X"; }  // differs
  // -cmp TARGET is TRUE (same) when byte-identical; TARGET is a field template (a bare
  // path is a literal). a.txt == twin.txt, a.txt != diff.txt.
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-cmp", Path("twin.txt")}), ElementsAre(Path("a.txt")));
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-cmp", Path("diff.txt")}), IsEmpty());
  // `! -cmp` selects files that differ (the "list changed files" idiom).
  EXPECT_THAT(RunExpr({"-name", "a.txt", "!", "-cmp", Path("diff.txt")}), ElementsAre(Path("a.txt")));
  // A missing / unreadable target counts as differing (not-same -> false).
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-cmp", Path("nope.txt")}), IsEmpty());
}

TEST_F(RunTest, CmpTargetIsAPerEntryTemplate) {
  // The target is rendered per entry, so {def.NAME} / {name} build it dynamically:
  // compare each file against a same-named file under a parallel directory.
  const std::string other = (fs::path(::testing::TempDir()) / "xff_cmp_other").string();
  std::error_code ec;
  fs::remove_all(other, ec);
  ASSERT_TRUE(fs::create_directories(other));
  { std::ofstream(fs::path(other) / "a.txt") << "a"; }         // identical to <root>/a.txt
  { std::ofstream(fs::path(other) / "b.md") << "DIFFERENT"; }  // differs from <root>/b.md ("b")
  // ! -cmp '{def.OTHER}/{name}' -> files whose twin under OTHER differs (b.md; a.txt matches).
  const std::vector<std::string> changed =
      RunArgvRecords({"--define=OTHER=" + other, root_.string(), "-type", "f", "!", "-cmp", "{def.OTHER}/{name}"});
  fs::remove_all(other, ec);
  // sub/c.txt has no counterpart under OTHER (missing -> differs); b.md differs; a.txt matches.
  EXPECT_THAT(changed, UnorderedElementsAre(Path("b.md"), Path("sub/c.txt")));
}

TEST_F(RunTest, ColorAutoStaysPlainWhenStdoutIsNotATty) {
  // The captured stdout here is a pipe, so auto (the default) leaves even a
  // directory uncolored; only --color=always would force escapes.
  EXPECT_THAT(RunArgvRecords({root_.string(), "-name", "sub"}), ElementsAre(Path("sub")));
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
  EXPECT_TRUE(fs::exists(root_ / "a.txt"));                   // --dry-run: nothing deleted
  EXPECT_THAT(records, UnorderedElementsAre(Path("a.txt")));  // but previewed
}

TEST_F(RunTest, SafeRefusesDelete) {
  const auto command = parser::Parse({"--safe", root_.string(), "-delete"});
  ASSERT_THAT(command, IsOk());
  const int errors = RunFind(*command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
  EXPECT_THAT(errors, 2);
  EXPECT_TRUE(fs::exists(root_ / "a.txt"));  // refused: nothing deleted
}

TEST_F(RunTest, ExecRunsCommandPerMatch) {
  // -exec /bin/sh -c 'echo > "{}.ran"' ; creates a marker beside each matched file.
  RunExpr({"-name", "a.txt", "-exec", "/bin/sh", "-c", "echo > \"{}.ran\"", ";"});
  EXPECT_TRUE(fs::exists(root_ / "a.txt.ran"));
}

TEST_F(RunTest, SafeRefusesExec) {
  const auto command = parser::Parse({"--safe", root_.string(), "-exec", "/bin/sh", "-c", "echo > \"{}.ran\"", ";"});
  ASSERT_THAT(command, IsOk());
  const int errors = RunFind(*command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
  EXPECT_THAT(errors, 2);
  EXPECT_FALSE(fs::exists(root_ / "a.txt.ran"));  // refused: command not run
}

TEST_F(RunTest, UnknownTimezoneIsRefusedBeforeTraversal) {
  // An unknown --timezone is a usage error refused before the walk (exit 2), like
  // the --safe guards above: reported via on_error, emitting nothing.
  const auto command = parser::Parse({"--timezone=Not/AZone", root_.string()});
  ASSERT_THAT(command, IsOk());
  std::string err_path;
  absl::Status err_status;
  bool emitted = false;
  const int errors = RunFind(
      *command, fs_, [&](std::string_view) { emitted = true; },
      [&](std::string_view path, absl::Status status) {
        err_path = std::string(path);
        err_status = status;
      });
  EXPECT_THAT(errors, 2);
  EXPECT_THAT(err_path, "--timezone");
  EXPECT_THAT(err_status, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(emitted) << "an invalid --timezone must not traverse";
}

TEST_F(RunTest, OversizedSizeUnitIsRefusedBeforeTraversal) {
  // -size with an over-64-bit unit (Z/Y/...) is a usage error refused before the
  // walk (exit 2), naming the limit -- not a silent per-entry no-match.
  const auto command = parser::Parse({root_.string(), "-size", "+1Z"});
  ASSERT_THAT(command, IsOk());
  absl::Status err_status;
  bool emitted = false;
  const int errors = RunFind(
      *command, fs_, [&](std::string_view) { emitted = true; },
      [&](std::string_view, absl::Status status) { err_status = status; });
  EXPECT_THAT(errors, 2);
  EXPECT_THAT(err_status, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("E (exabyte)")));
  EXPECT_FALSE(emitted) << "a malformed -size must not traverse";
}

TEST_F(RunTest, BlockSizeRedefinesTheBareSizeUnit) {
  // A 1000-byte file is 2 blocks at the default 512 (so -size 1 misses, -size 2
  // hits) but 1 block under --block-size=4k (so -size 1 hits). Proves the global
  // redefines the bare/`b` -size unit end to end.
  { std::ofstream(root_ / "kilo.bin", std::ios::binary) << std::string(1'000, 'x'); }
  EXPECT_THAT(RunArgvRecords({root_.string(), "-name", "kilo.bin", "-size", "1"}), IsEmpty());
  EXPECT_THAT(RunArgvRecords({root_.string(), "-name", "kilo.bin", "-size", "2"}), ElementsAre(Path("kilo.bin")));
  EXPECT_THAT(
      RunArgvRecords({"--block-size=4k", root_.string(), "-name", "kilo.bin", "-size", "1"}),
      ElementsAre(Path("kilo.bin")));
}

TEST_F(RunTest, InvalidBlockSizeIsRefusedBeforeTraversal) {
  const auto command = parser::Parse({"--block-size=0", root_.string()});
  ASSERT_THAT(command, IsOk());
  absl::Status err_status;
  bool emitted = false;
  const int errors = RunFind(
      *command, fs_, [&](std::string_view) { emitted = true; },
      [&](std::string_view, absl::Status status) { err_status = status; });
  EXPECT_THAT(errors, 2);
  EXPECT_THAT(err_status, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("positive")));
  EXPECT_FALSE(emitted) << "an invalid --block-size must not traverse";
}

TEST_F(RunTest, ValidTimezoneIsAcceptedAndTheRunProceeds) {
  // A valid --timezone resolves and the run proceeds normally (here it does not
  // change the result, just proving the flag is accepted end to end).
  const auto command = parser::Parse({"--timezone=UTC", root_.string(), "-name", "a.txt"});
  ASSERT_THAT(command, IsOk());
  std::vector<std::string> records;
  const int errors = RunFind(
      *command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(errors, 0);
  EXPECT_THAT(records, UnorderedElementsAre(Path("a.txt")));
}

TEST_F(RunTest, TimezoneAppliesToTimeFieldFormatting) {
  // --timezone reaches time-field formatting too: {mtime:%z} is the numeric zone
  // offset, so under --timezone=UTC it is "+0000" regardless of the host's zone.
  const auto command = parser::Parse({"--timezone=UTC", "--template={mtime:%z}", root_.string(), "-name", "a.txt"});
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
  EXPECT_THAT(records, UnorderedElementsAre("+0000"));
}

TEST_F(RunTest, TzIsAnAliasForTimezone) {
  // --tz=ZONE is the short alias of --timezone=ZONE: under --tz=UTC the {mtime:%z}
  // numeric offset is "+0000" regardless of the host zone, just as --timezone=UTC.
  EXPECT_THAT(
      RunArgvRecords({"--tz=UTC", "--template={mtime:%z}", root_.string(), "-name", "a.txt"}),
      UnorderedElementsAre("+0000"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, FixedOffsetTimezoneAppliesToFormatting) {
  // A fixed UTC offset (+05:30) is accepted as a zone spec and reaches time-field
  // formatting: {mtime:%z} renders the zone's numeric offset, "+0530".
  EXPECT_THAT(
      RunArgvRecords({"--tz=+05:30", "--template={mtime:%z}", root_.string(), "-name", "a.txt"}),
      UnorderedElementsAre("+0530"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, AnyMatchIsTrueWhenExpressionMatches) {
  const auto command = parser::Parse({root_.string(), "-name", "a.txt"});
  ASSERT_THAT(command, IsOk());
  bool matched = false;
  RunFind(*command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {}, std::nullopt, &matched);
  EXPECT_THAT(matched, IsTrue());
}

TEST_F(RunTest, AnyMatchIsFalseWhenNothingMatches) {
  const auto command = parser::Parse({root_.string(), "-name", "no-such-file.zzz"});
  ASSERT_THAT(command, IsOk());
  bool matched = false;
  RunFind(*command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {}, std::nullopt, &matched);
  EXPECT_THAT(matched, IsFalse());
}

TEST_F(RunTest, AnyMatchReflectsExpressionNotEmittedOutput) {
  // any_match is the expression's truth, not output: with --implicit-print=no, a.txt
  // matches but nothing is emitted, yet any_match is still true (so --quiet on an
  // action-only expression like `-exec` still reports the match).
  const auto command = parser::Parse({"--implicit-print=no", root_.string(), "-name", "a.txt"});
  ASSERT_THAT(command, IsOk());
  std::vector<std::string> records;
  bool matched = false;
  RunFind(
      *command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [](std::string_view, absl::Status) {}, std::nullopt, &matched);
  EXPECT_THAT(records, IsEmpty());
  EXPECT_THAT(matched, IsTrue());
}

TEST_F(RunTest, TimeFormatGlobalSetsTheBareTimeFieldDefault) {
  // --time-format=epoch makes a bare {mtime} render as Unix seconds (all digits,
  // no date dashes), proving the global threads through to time-field formatting.
  const auto command = parser::Parse({"--time-format=epoch", "--template={mtime}", root_.string(), "-name", "a.txt"});
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
  // epoch is all digits; a date format would carry dashes. ElementsAre folds the
  // single-match count and the content check into one matcher.
  EXPECT_THAT(records, ElementsAre(Not(HasSubstr("-"))));
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
  EXPECT_THAT(content, "a.txt");  // {1}="a", {2}="txt"
}

TEST_F(RunTest, DefinePopulatesDefNamespace) {
  // --define=NAME=VALUE surfaces as {def.NAME} in --template output (last wins).
  const auto command = parser::Parse(
      {"--define=label=old", "--define=label=new", "--template={def.label}:{name}", root_.string(), "-name", "a.txt"});
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
  // -capture runs a command per match (with {} -> path) and binds its stdout to
  // {capture.NAME}; --template then prints it.
  const auto command = parser::Parse(
      {"--template={capture.base}", root_.string(), "-name", "a.txt", "-capture=base", "/bin/sh", "-c", "basename {}",
       ";"});
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
  // A later -capture command references an earlier capture's {capture.*}.
  const auto command = parser::Parse(
      {"--template={capture.b}", root_.string(), "-name", "a.txt", "-capture=a", "/bin/sh", "-c", "printf X", ";",
       "-capture=b", "/bin/sh", "-c", "printf {capture.a}Y", ";"});
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
  EXPECT_THAT(records, UnorderedElementsAre("XY"));  // b = {capture.a}("X") + "Y"
}

TEST_F(RunTest, DuplicateCaptureNameIsErrorByDefault) {
  // Two -capture actions binding the same NAME, no --capture-override -> exit 2,
  // reported before traversal (silent clobbering would mean wrong data).
  const auto command = parser::Parse(
      {root_.string(), "-capture=x", "/bin/sh", "-c", "printf a", ";", "-capture=x", "/bin/sh", "-c", "printf b", ";"});
  ASSERT_THAT(command, IsOk());
  int errors = 0;
  const int code = RunFind(*command, fs_, [](std::string_view) {}, [&](std::string_view, absl::Status) { ++errors; });
  EXPECT_THAT(code, 2);
  EXPECT_THAT(errors, 1);
}

TEST_F(RunTest, CaptureOverrideAllowsDuplicateNameLastWins) {
  const auto command = parser::Parse(
      {"--capture-override", "--template={capture.x}", root_.string(), "-name", "a.txt", "-capture=x", "/bin/sh", "-c",
       "printf a", ";", "-capture=x", "/bin/sh", "-c", "printf b", ";"});
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
  EXPECT_THAT(records, UnorderedElementsAre("b"));  // last -capture wins under --capture-override
}

TEST_F(RunTest, UnusedCaptureIsError) {
  // -capture=x but {capture.x} is referenced nowhere -> exit 2 before traversal.
  const auto command =
      parser::Parse({root_.string(), "-name", "a.txt", "-capture=x", "/bin/sh", "-c", "printf a", ";"});
  ASSERT_THAT(command, IsOk());
  int errors = 0;
  const int code = RunFind(*command, fs_, [](std::string_view) {}, [&](std::string_view, absl::Status) { ++errors; });
  EXPECT_THAT(code, 2);
  EXPECT_THAT(errors, 1);
}

TEST_F(RunTest, CaptureUsedByLaterExecIsNotFlagged) {
  // {capture.x} referenced in a later -exec counts as used -> no unused error.
  const auto command = parser::Parse(
      {"--exec-fields", root_.string(), "-name", "a.txt", "-capture=x", "/bin/sh", "-c", "printf a", ";", "-exec",
       "/bin/sh", "-c", "test \"{capture.x}\" = a", ";"});
  ASSERT_THAT(command, IsOk());
  int errors = 0;
  const int code = RunFind(*command, fs_, [](std::string_view) {}, [&](std::string_view, absl::Status) { ++errors; });
  EXPECT_THAT(code, 0);  // used by the -exec, so not flagged
  EXPECT_THAT(errors, 0);
}

TEST_F(RunTest, ImplicitPrintNoSuppressesDefaultPrint) {
  // No action, so find would print -- --implicit-print=no forces it off.
  const auto command = parser::Parse({"--implicit-print=no", root_.string(), "-name", "a.txt"});
  ASSERT_THAT(command, IsOk());
  std::vector<std::string> records;
  RunFind(
      *command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, IsEmpty());
}

TEST_F(RunTest, ImplicitPrintYesPrintsAlongsideAction) {
  // -exec would suppress the implicit print; --implicit-print=yes forces it on.
  const auto command =
      parser::Parse({"--implicit-print=yes", root_.string(), "-name", "a.txt", "-exec", "/bin/sh", "-c", "true", ";"});
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
  EXPECT_THAT(records, UnorderedElementsAre(Path("a.txt")));
}

// The default --summary output is a right-aligned human table (grouped digits);
// these assert the stable --format=jsonl machine rows instead, so the exact counts
// are checked without depending on column padding. The aligned human rendering is
// covered end to end by cli/summary_test.sh.
TEST_F(RunTest, SummaryOverallReducesMatchesToACountAndSize) {
  // --summary suppresses the per-match print and emits one total row: a.txt and
  // sub/c.txt match (1 byte each), so 2 matches / 2 bytes.
  EXPECT_THAT(
      RunArgvRecords({"--summary", "--format=jsonl", root_.string(), "-name", "*.txt"}),
      ElementsAre(R"({"group":"total","count":2,"bytes":2})"));
}

TEST_F(RunTest, SummaryByTypeGroupsThenTotals) {
  // --summary=type over the three files (1 byte each): one "file" group, then total.
  EXPECT_THAT(
      RunArgvRecords({"--summary=type", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(R"({"group":"file","count":3,"bytes":3})", R"({"group":"total","count":3,"bytes":3})"));
}

TEST_F(RunTest, SummaryByExtensionGroupsSortedThenTotals) {
  // --summary=ext over the files: "md" (b.md) sorts before "txt" (a.txt, sub/c.txt).
  EXPECT_THAT(
      RunArgvRecords({"--summary=ext", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"({"group":"md","count":1,"bytes":1})", R"({"group":"txt","count":2,"bytes":2})",
          R"({"group":"total","count":3,"bytes":3})"));
}

TEST_F(RunTest, SummaryTopKeepsTheLargestGroupsBySize) {
  // --top=1: keep the largest group by size (txt, 2 bytes) and drop md (1 byte),
  // ordered by size; the total row still counts every matched group.
  EXPECT_THAT(
      RunArgvRecords({"--summary=ext", "--top=1", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(R"({"group":"txt","count":2,"bytes":2})", R"({"group":"total","count":3,"bytes":3})"));
}

TEST_F(RunTest, LsEmitsOneLinePerMatchAndSuppressesImplicitPrint) {
  // -ls is an action, so it suppresses the implicit -print: exactly one line (the
  // ls-style listing) for the match, containing its path. The exact columns are
  // umask/fs-dependent (covered deterministically in evaluate_test); here we just
  // confirm the end-to-end wiring and the print suppression.
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-ls"}), ElementsAre(HasSubstr(Path("a.txt"))));
}

// Minimal in-memory FileSystem: a root directory holding one regular file whose
// metadata carries NO birth time. The real local FS records btime on macOS/Linux,
// so it cannot reproduce the "-Btime where birth time is unrecorded" impossible
// task; this can.
class NoBtimeFs : public vfs::FileSystem {
 public:
  explicit NoBtimeFs(std::string root) : root_(std::move(root)) {}

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view dir) const override {
    if (std::string(dir) != root_) {
      return absl::NotFoundError("NoBtimeFs: no such directory");
    }
    return std::vector<vfs::Entry>{
        vfs::Entry{.path = root_ + "/f.txt", .name = "f.txt", .type = vfs::FileType::kRegular}};
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool /*follow_symlinks*/) const override {
    vfs::Metadata md;
    md.type = std::string(path) == root_ ? vfs::FileType::kDirectory : vfs::FileType::kRegular;
    return md;  // btime deliberately left empty (unrecorded)
  }

  absl::Status Remove(std::string_view) const override { return absl::OkStatus(); }

  bool Access(std::string_view, vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::InvalidArgumentError("NoBtimeFs: not a symlink");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return std::string("fakefs"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override { return std::string(); }

 private:
  std::string root_;
};

TEST_F(RunTest, BtimeOnEntryWithoutBirthtimeFailsByDefault) {
  // Impossible task: -Btime against a filesystem that does not record birth time is
  // a hard error (exit 2), reported once with a self-documenting message.
  const NoBtimeFs fs("/fake");
  const auto command = parser::Parse({"/fake", "-Btime", "1"});
  ASSERT_THAT(command, IsOk());
  int reports = 0;
  std::string message;
  const int errors = RunFind(
      *command, fs, [](std::string_view) {},
      [&](std::string_view, absl::Status status) {
        ++reports;
        message = std::string(status.message());
      });
  EXPECT_THAT(errors, Not(0));  // hard error
  EXPECT_THAT(reports, 1);      // reported once, not per entry
  EXPECT_THAT(message, HasSubstr("birth time"));
}

TEST_F(RunTest, SkipUnsupportedDowngradesImpossibleBtimeToWarnAndSkip) {
  // --skip-unsupported turns the same impossible task into a warning + skip: the
  // run still reports once (so the user knows), but it is not an error (exit 0).
  const NoBtimeFs fs("/fake");
  const auto command = parser::Parse({"--skip-unsupported", "/fake", "-Btime", "1"});
  ASSERT_THAT(command, IsOk());
  int reports = 0;
  const int errors = RunFind(*command, fs, [](std::string_view) {}, [&](std::string_view, absl::Status) { ++reports; });
  EXPECT_THAT(errors, 0);   // skipped -> not an error
  EXPECT_THAT(reports, 1);  // but warned once
}

// In-memory FileSystem on a case-FOLDING volume (IsCaseSensitive -> false): a root
// holding one mixed-case regular file (Foo.txt). It reports case-insensitive
// regardless of the host runner, so the FS-native -name matching / --exact tests
// below are deterministic on case-sensitive CI (ext4) too.
class CaseFoldFs : public vfs::FileSystem {
 public:
  explicit CaseFoldFs(std::string root) : root_(std::move(root)) {}

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view dir) const override {
    if (std::string(dir) != root_) {
      return absl::NotFoundError("CaseFoldFs: no such directory");
    }
    return std::vector<vfs::Entry>{
        vfs::Entry{.path = root_ + "/Foo.txt", .name = "Foo.txt", .type = vfs::FileType::kRegular}};
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool /*follow_symlinks*/) const override {
    vfs::Metadata md;
    md.type = std::string(path) == root_ ? vfs::FileType::kDirectory : vfs::FileType::kRegular;
    return md;
  }

  absl::Status Remove(std::string_view) const override { return absl::OkStatus(); }

  bool Access(std::string_view, vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::InvalidArgumentError("CaseFoldFs: not a symlink");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return std::string("fakefs"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return false; }  // a folding volume

  absl::StatusOr<std::string> ReadContent(std::string_view) const override { return std::string(); }

 private:
  std::string root_;
};

// Runs `-name <pattern>` over a CaseFoldFs (holding Foo.txt) in `style`, with or
// without --exact, and returns the concatenated emitted output.
std::string RunNameOnCaseFoldVolume(std::string_view pattern, std::optional<registry::Style> style, bool exact) {
  const CaseFoldFs fs("/fake");
  std::vector<std::string> args;
  if (exact) {
    args.emplace_back("--exact");
  }
  args.insert(args.end(), {"/fake", "-name", std::string(pattern)});
  const auto command = parser::Parse(args);
  EXPECT_THAT(command, IsOk());
  std::string out;
  const int errors = RunFind(
      *command, fs, [&out](std::string_view record) { out += record; }, [](std::string_view, absl::Status) {}, style);
  EXPECT_THAT(errors, 0);
  return out;
}

TEST_F(RunTest, XffStyleFoldsNameOnCaseFoldingVolume) {
  // FS-native matching: the xff style matches -name the way the volume resolves
  // names, so a lower-case pattern matches the mixed-case Foo.txt on a folding FS.
  EXPECT_THAT(RunNameOnCaseFoldVolume("foo.txt", registry::Style::kXff, /*exact=*/false), HasSubstr("Foo.txt"));
}

TEST_F(RunTest, ExactOptsOutOfFsNativeFolding) {
  // --exact forces verbatim byte-exact matching even on a folding volume.
  EXPECT_THAT(RunNameOnCaseFoldVolume("foo.txt", registry::Style::kXff, /*exact=*/true), IsEmpty());
}

TEST_F(RunTest, FindStyleIsAlwaysByteExact) {
  // The find style is drop-in faithful: no FS-native folding, so a lower-case
  // pattern does not match Foo.txt regardless of the volume.
  EXPECT_THAT(RunNameOnCaseFoldVolume("foo.txt", registry::Style::kFind, /*exact=*/false), IsEmpty());
}

TEST_F(RunTest, InProcessDefaultStyleIsByteExact) {
  // std::nullopt style (the conservative in-process default) does not fold either;
  // FS-native matching is opt-in via the xff style the CLI resolves.
  EXPECT_THAT(RunNameOnCaseFoldVolume("foo.txt", std::nullopt, /*exact=*/false), IsEmpty());
}

TEST_F(RunTest, ExactCaseNameMatchesRegardlessOfFolding) {
  // The exact-case name matches in every style / with --exact -- folding only
  // widens what matches, it never stops the verbatim name from matching.
  EXPECT_THAT(RunNameOnCaseFoldVolume("Foo.txt", registry::Style::kXff, /*exact=*/false), HasSubstr("Foo.txt"));
  EXPECT_THAT(RunNameOnCaseFoldVolume("Foo.txt", registry::Style::kXff, /*exact=*/true), HasSubstr("Foo.txt"));
  EXPECT_THAT(RunNameOnCaseFoldVolume("Foo.txt", registry::Style::kFind, /*exact=*/false), HasSubstr("Foo.txt"));
}

TEST_F(RunTest, GrepEmitsPathLineTextAcrossTheWalk) {
  // -grep is an action, so it suppresses the implicit path-print and emits one
  // record per matching line, path:line:text, over the whole traversal.
  { std::ofstream(root_ / "a.txt") << "alpha\nTODO one\nbeta\nTODO two\n"; }  // overwrite the fixture's "a"
  EXPECT_THAT(
      RunExpr({"-name", "a.txt", "-grep", "TODO"}),
      ElementsAre(Path("a.txt") + ":2:TODO one", Path("a.txt") + ":4:TODO two"));
}

TEST_F(RunTest, GrepRegextypeExactMatchesLiterally) {
  { std::ofstream(root_ / "a.txt") << "price 3.50\nprice 3X50\n"; }
  // --regextype=EXACT (a leading global) makes '.' a literal, so only the real 3.50.
  EXPECT_THAT(
      RunArgvRecords({"--regextype=EXACT", root_.string(), "-name", "a.txt", "-grep", "3.50"}),
      ElementsAre(Path("a.txt") + ":1:price 3.50"));
}

TEST_F(RunTest, GrepRegextypeDefaultIsRe2) {
  { std::ofstream(root_ / "a.txt") << "price 3.50\nprice 3X50\n"; }
  // Default (RE2): '.' is a wildcard, so both lines match.
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-name", "a.txt", "-grep", "3.50"}),
      ElementsAre(Path("a.txt") + ":1:price 3.50", Path("a.txt") + ":2:price 3X50"));
}

TEST_F(RunTest, UnsupportedRegextypeIsAUsageError) {
  // MATCH / PCRE are reserved (#85): a usage error refused before the walk (exit 2).
  EXPECT_THAT(RunArgvRecords({"--regextype=MATCH", root_.string(), "-grep", "x"}), IsEmpty());
  EXPECT_THAT(last_errors_, Not(0));
}

TEST_F(RunTest, GrepFormatRendersCustomTemplate) {
  // -grep=FORMAT overrides the default path:line:text with a field template.
  { std::ofstream(root_ / "a.txt") << "alpha\nTODO one\nbeta\n"; }
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-grep={line}|{text}", "TODO"}), ElementsAre("2|TODO one"));
}

TEST_F(RunTest, GrepCountEmitsPerFileCount) {
  // --count / -c (a leading global): path:count per file with matches, not the lines.
  { std::ofstream(root_ / "a.txt") << "TODO 1\nx\nTODO 2\n"; }
  EXPECT_THAT(
      RunArgvRecords({"--count", root_.string(), "-name", "a.txt", "-grep", "TODO"}),
      ElementsAre(Path("a.txt") + ":2"));
}

}  // namespace
}  // namespace xff::engine
