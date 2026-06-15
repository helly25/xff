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

// popen()/pclose() are POSIX, hidden by glibc under the strict -std=c++23 build.
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#include <grp.h>      // getgrgid()/struct group for the -group oracle
#include <pwd.h>      // getpwuid()/struct passwd for the -user oracle
#include <unistd.h>   // geteuid()/getegid() for the -uid/-gid and -user/-group oracles

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "mbo/testing/status.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/local_fs.h"

namespace xff {
namespace {

namespace fs = std::filesystem;

using ::mbo::testing::IsOk;
using ::testing::ElementsAreArray;

// Wraps `arg` in single quotes so the shell passes it to `find` verbatim
// (e.g. globs are not expanded by the shell).
std::string ShellQuote(std::string_view arg) {
  std::string out = "'";
  for (const char c : arg) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

std::vector<std::string> SplitLines(std::string_view text) {
  std::vector<std::string> lines;
  std::string current;
  for (const char c : text) {
    if (c == '\n') {
      lines.push_back(current);
      current.clear();
    } else {
      current += c;
    }
  }
  if (!current.empty()) {
    lines.push_back(current);
  }
  return lines;
}

// Asserts that `xff <root> <expr>` produces the same set of paths as the system
// `find`. find is the oracle on whichever platform the test runs (GNU on Linux,
// BSD on macOS), so only universally-portable predicates belong in the matrix.
struct ConformanceTest : ::testing::Test {
  void SetUp() override {
    root_ = fs::path(::testing::TempDir()) /
            (std::string("xff_conf_") + ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::error_code ec;
    fs::remove_all(root_, ec);
    ASSERT_TRUE(fs::create_directories(root_ / "sub"));
    { std::ofstream(root_ / "a.txt") << "a"; }
    { std::ofstream(root_ / "b.md") << "bb"; }
    { std::ofstream(root_ / "sub" / "c.txt") << "ccc"; }
    fs::create_symlink("a.txt", root_ / "link");
    // Known modes so -perm cases are meaningful (find and xff both lstat these).
    fs::permissions(root_ / "a.txt", static_cast<fs::perms>(0644));
    fs::permissions(root_ / "b.md", static_cast<fs::perms>(0640));
    fs::permissions(root_ / "sub" / "c.txt", static_cast<fs::perms>(0600));
    { std::ofstream(root_ / "empty.txt"); }  // 0 bytes, for -empty
    fs::create_directory(root_ / "emptydir", ec);
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }

  // Runs `find <root> <expr...>`, returning its sorted output lines, or nullopt
  // if `find` could not be run (so the test skips rather than fails).
  std::optional<std::vector<std::string>> SystemFind(const std::vector<std::string>& expr) {
    std::string command = absl::StrCat("find ", ShellQuote(root_.string()));
    for (const std::string& token : expr) {
      absl::StrAppend(&command, " ", ShellQuote(token));
    }
    absl::StrAppend(&command, " 2>/dev/null");

    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
      return std::nullopt;
    }
    std::string output;
    char chunk[4096];
    for (std::size_t n = ::fread(chunk, 1, sizeof(chunk), pipe); n > 0; n = ::fread(chunk, 1, sizeof(chunk), pipe)) {
      output.append(chunk, n);
    }
    if (::pclose(pipe) != 0) {
      return std::nullopt;
    }
    std::vector<std::string> lines = SplitLines(output);
    std::sort(lines.begin(), lines.end());
    return lines;
  }

  // Runs xff's engine over the same root + expression, returning sorted output.
  std::vector<std::string> Xff(const std::vector<std::string>& expr) {
    std::vector<std::string> argv = {root_.string()};
    argv.insert(argv.end(), expr.begin(), expr.end());
    const auto command = parser::Parse(argv);
    EXPECT_THAT(command, IsOk());
    std::vector<std::string> lines;
    if (command.ok()) {
      engine::RunFind(
          *command, fs_,
          [&](std::string_view record) {
            std::string text(record);
            if (!text.empty() && (text.back() == '\n' || text.back() == '\0')) {
              text.pop_back();
            }
            lines.push_back(std::move(text));
          },
          [](std::string_view, const absl::Status&) {});
    }
    std::sort(lines.begin(), lines.end());
    return lines;
  }

  void ExpectMatchesFind(const std::vector<std::string>& expr) {
    const std::optional<std::vector<std::string>> expected = SystemFind(expr);
    if (!expected.has_value()) {
      GTEST_SKIP() << "system `find` unavailable";
    }
    EXPECT_THAT(Xff(expr), ElementsAreArray(*expected));
  }

  vfs::LocalFs fs_;
  fs::path root_;
};

TEST_F(ConformanceTest, PrintAll) { ExpectMatchesFind({}); }
TEST_F(ConformanceTest, ExplicitPrint) { ExpectMatchesFind({"-print"}); }
TEST_F(ConformanceTest, TypeFile) { ExpectMatchesFind({"-type", "f"}); }
TEST_F(ConformanceTest, TypeDirectory) { ExpectMatchesFind({"-type", "d"}); }
TEST_F(ConformanceTest, TypeSymlink) { ExpectMatchesFind({"-type", "l"}); }
TEST_F(ConformanceTest, NameGlob) { ExpectMatchesFind({"-name", "*.txt"}); }
TEST_F(ConformanceTest, NotTypeDirectory) { ExpectMatchesFind({"!", "-type", "d"}); }
TEST_F(ConformanceTest, OrExpression) { ExpectMatchesFind({"-name", "*.txt", "-o", "-type", "d"}); }
TEST_F(ConformanceTest, PathGlob) { ExpectMatchesFind({"-path", "*/sub/*"}); }
TEST_F(ConformanceTest, AndChain) { ExpectMatchesFind({"-type", "f", "-name", "*.md"}); }
TEST_F(ConformanceTest, SizeExactBytes) { ExpectMatchesFind({"-size", "1c"}); }
TEST_F(ConformanceTest, SizeGreaterBytes) { ExpectMatchesFind({"-size", "+1c"}); }
TEST_F(ConformanceTest, SizeLessBytes) { ExpectMatchesFind({"-size", "-3c"}); }
TEST_F(ConformanceTest, PermExact) { ExpectMatchesFind({"-perm", "644"}); }
TEST_F(ConformanceTest, PermExactOther) { ExpectMatchesFind({"-perm", "600"}); }
TEST_F(ConformanceTest, PermAllBitsOwnerWrite) { ExpectMatchesFind({"-perm", "-200"}); }
TEST_F(ConformanceTest, PermAllBitsReadable) { ExpectMatchesFind({"-perm", "-044"}); }
TEST_F(ConformanceTest, MaxDepthOne) { ExpectMatchesFind({"-maxdepth", "1"}); }
TEST_F(ConformanceTest, MaxDepthTwo) { ExpectMatchesFind({"-maxdepth", "2"}); }
TEST_F(ConformanceTest, MinDepthOne) { ExpectMatchesFind({"-mindepth", "1"}); }
TEST_F(ConformanceTest, MaxDepthWithType) { ExpectMatchesFind({"-maxdepth", "1", "-type", "f"}); }
TEST_F(ConformanceTest, Empty) { ExpectMatchesFind({"-empty"}); }
TEST_F(ConformanceTest, LinksOne) { ExpectMatchesFind({"-links", "1"}); }
TEST_F(ConformanceTest, LinksMoreThanOne) { ExpectMatchesFind({"-links", "+1"}); }
TEST_F(ConformanceTest, NewerThanReferenceFile) { ExpectMatchesFind({"-newer", (root_ / "b.md").string()}); }
TEST_F(ConformanceTest, UidMatchesCurrentUser) { ExpectMatchesFind({"-uid", std::to_string(::geteuid())}); }
TEST_F(ConformanceTest, GidMatchesCurrentGroup) { ExpectMatchesFind({"-gid", std::to_string(::getegid())}); }
TEST_F(ConformanceTest, UserMatchesCurrentUser) {
  const struct passwd* const pw = ::getpwuid(::geteuid());
  if (pw == nullptr) GTEST_SKIP() << "no passwd entry for euid";
  ExpectMatchesFind({"-user", pw->pw_name});
}
TEST_F(ConformanceTest, GroupMatchesCurrentGroup) {
  const struct group* const gr = ::getgrgid(::getegid());
  if (gr == nullptr) GTEST_SKIP() << "no group entry for egid";
  ExpectMatchesFind({"-group", gr->gr_name});
}
TEST_F(ConformanceTest, ParenGrouping) { ExpectMatchesFind({"(", "-type", "f", "-o", "-type", "d", ")"}); }
// The comma operator is a GNU extension; BSD find rejects it, so these skip on macOS.
TEST_F(ConformanceTest, CommaListImplicitPrint) { ExpectMatchesFind({"-name", "*.txt", ",", "-name", "*.md"}); }
TEST_F(ConformanceTest, CommaWithExplicitAction) { ExpectMatchesFind({"-type", "f", ",", "-print"}); }

}  // namespace
}  // namespace xff
