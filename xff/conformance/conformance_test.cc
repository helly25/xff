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
# define _GNU_SOURCE 1
#endif

#include <grp.h>     // getgrgid()/struct group for the -group oracle
#include <pwd.h>     // getpwuid()/struct passwd for the -user oracle
#include <unistd.h>  // geteuid()/getegid() for the -uid/-gid and -user/-group oracles

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
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
    root_ = fs::path(::testing::TempDir())
            / (std::string("xff_conf_") + ::testing::UnitTest::GetInstance()->current_test_info()->name());
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

  // Runs `find <globals...> <root> <expr...>`, returning its sorted output lines,
  // or nullopt if `find` could not be run (so the test skips rather than fails).
  std::optional<std::vector<std::string>> SystemFind(
      const std::vector<std::string>& globals,
      const std::vector<std::string>& expr) {
    std::string command = "find";
    for (const std::string& global : globals) {
      absl::StrAppend(&command, " ", ShellQuote(global));
    }
    absl::StrAppend(&command, " ", ShellQuote(root_.string()));
    for (const std::string& token : expr) {
      absl::StrAppend(&command, " ", ShellQuote(token));
    }
    absl::StrAppend(&command, " 2>/dev/null");

    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
      return std::nullopt;
    }
    std::string output;
    std::array<char, 4'096> chunk;
    for (std::size_t n = ::fread(chunk.data(), 1, chunk.size(), pipe); n > 0;
         n = ::fread(chunk.data(), 1, chunk.size(), pipe)) {
      output.append(chunk.data(), n);
    }
    if (::pclose(pipe) != 0) {
      return std::nullopt;
    }
    std::vector<std::string> lines = SplitLines(output);
    std::sort(lines.begin(), lines.end());
    return lines;
  }

  // Runs xff's engine over the same globals + root + expression, sorted.
  std::vector<std::string> Xff(const std::vector<std::string>& globals, const std::vector<std::string>& expr) {
    std::vector<std::string> argv = globals;
    argv.push_back(root_.string());
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
          [](std::string_view, absl::Status) {});
    }
    std::sort(lines.begin(), lines.end());
    return lines;
  }

  void ExpectMatchesFind(const std::vector<std::string>& expr) { ExpectMatchesFind({}, expr); }

  void ExpectMatchesFind(const std::vector<std::string>& globals, const std::vector<std::string>& expr) {
    const std::optional<std::vector<std::string>> expected = SystemFind(globals, expr);
    if (!expected.has_value()) {
      GTEST_SKIP() << "system `find` unavailable";
    }
    EXPECT_THAT(Xff(globals, expr), ElementsAreArray(*expected));
  }

  vfs::LocalFs fs_;
  fs::path root_;
};

TEST_F(ConformanceTest, PrintAll) {
  ExpectMatchesFind({});
}

TEST_F(ConformanceTest, ExplicitPrint) {
  ExpectMatchesFind({"-print"});
}

TEST_F(ConformanceTest, TypeFile) {
  ExpectMatchesFind({"-type", "f"});
}

TEST_F(ConformanceTest, TypeDirectory) {
  ExpectMatchesFind({"-type", "d"});
}

TEST_F(ConformanceTest, TypeSymlink) {
  ExpectMatchesFind({"-type", "l"});
}

// Note: comma-separated -type lists ("f,d") are deliberately NOT conformance-
// tested. They are a GNU extension, and BSD find (macOS) silently accepts "f,d"
// as plain "f" rather than erroring, so the oracle diverges by platform. The
// GNU semantics are covered by the engine unit test instead.

// -lname is GNU-only; BSD find (macOS) errors, so SystemFind returns nullopt and
// this skips. The fixture's `link` targets `a.txt`.
TEST_F(ConformanceTest, LnameMatchesSymlinkTarget) {
  ExpectMatchesFind({"-lname", "a.txt"});
}

TEST_F(ConformanceTest, LnameGlobMatchesSymlinkTarget) {
  ExpectMatchesFind({"-lname", "*.txt"});
}

TEST_F(ConformanceTest, IlnameFoldsCase) {
  ExpectMatchesFind({"-ilname", "A.TXT"});
}

// -xtype follows symlinks: the fixture's `link` targets the regular file a.txt,
// so -xtype f includes it but -type f does not. (GNU find; skips on BSD find.)
TEST_F(ConformanceTest, XtypeFileFollowsLink) {
  ExpectMatchesFind({"-xtype", "f"});
}

TEST_F(ConformanceTest, XtypeDirectory) {
  ExpectMatchesFind({"-xtype", "d"});
}

TEST_F(ConformanceTest, XtypeSymlinkOnlyBroken) {
  ExpectMatchesFind({"-xtype", "l"});
}

TEST_F(ConformanceTest, NameGlob) {
  ExpectMatchesFind({"-name", "*.txt"});
}

TEST_F(ConformanceTest, NotTypeDirectory) {
  ExpectMatchesFind({"!", "-type", "d"});
}

TEST_F(ConformanceTest, OrExpression) {
  ExpectMatchesFind({"-name", "*.txt", "-o", "-type", "d"});
}

TEST_F(ConformanceTest, PathGlob) {
  ExpectMatchesFind({"-path", "*/sub/*"});
}

// -wholename/-iwholename are GNU synonyms for -path/-ipath; BSD find lacks them,
// so SystemFind returns nullopt and these skip on macOS.
TEST_F(ConformanceTest, WholenameGlob) {
  ExpectMatchesFind({"-wholename", "*/sub/*"});
}

TEST_F(ConformanceTest, IwholenameFoldsCase) {
  ExpectMatchesFind({"-iwholename", "*/SUB/*"});
}

TEST_F(ConformanceTest, AndChain) {
  ExpectMatchesFind({"-type", "f", "-name", "*.md"});
}

TEST_F(ConformanceTest, SizeExactBytes) {
  ExpectMatchesFind({"-size", "1c"});
}

TEST_F(ConformanceTest, SizeGreaterBytes) {
  ExpectMatchesFind({"-size", "+1c"});
}

TEST_F(ConformanceTest, SizeLessBytes) {
  ExpectMatchesFind({"-size", "-3c"});
}

TEST_F(ConformanceTest, PermExact) {
  ExpectMatchesFind({"-perm", "644"});
}

TEST_F(ConformanceTest, PermExactOther) {
  ExpectMatchesFind({"-perm", "600"});
}

TEST_F(ConformanceTest, PermAllBitsOwnerWrite) {
  ExpectMatchesFind({"-perm", "-200"});
}

TEST_F(ConformanceTest, PermAllBitsReadable) {
  ExpectMatchesFind({"-perm", "-044"});
}

TEST_F(ConformanceTest, MaxDepthOne) {
  ExpectMatchesFind({"-maxdepth", "1"});
}

TEST_F(ConformanceTest, MaxDepthTwo) {
  ExpectMatchesFind({"-maxdepth", "2"});
}

TEST_F(ConformanceTest, MinDepthOne) {
  ExpectMatchesFind({"-mindepth", "1"});
}

TEST_F(ConformanceTest, MaxDepthWithType) {
  ExpectMatchesFind({"-maxdepth", "1", "-type", "f"});
}

TEST_F(ConformanceTest, Empty) {
  ExpectMatchesFind({"-empty"});
}

TEST_F(ConformanceTest, LinksOne) {
  ExpectMatchesFind({"-links", "1"});
}

TEST_F(ConformanceTest, SamefileMatchesHardLink) {
  std::error_code ec;
  fs::create_hard_link(root_ / "a.txt", root_ / "a-hardlink", ec);
  if (ec) {
    GTEST_SKIP() << "hard links unsupported on this filesystem";
  }
  // -samefile a.txt must match a.txt and its hard link (same inode + device).
  ExpectMatchesFind({"-samefile", (root_ / "a.txt").string()});
}

TEST_F(ConformanceTest, LinksMoreThanOne) {
  ExpectMatchesFind({"-links", "+1"});
}

TEST_F(ConformanceTest, NewerThanReferenceFile) {
  ExpectMatchesFind({"-newer", (root_ / "b.md").string()});
}

TEST_F(ConformanceTest, UidMatchesCurrentUser) {
  ExpectMatchesFind({"-uid", std::to_string(::geteuid())});
}

TEST_F(ConformanceTest, GidMatchesCurrentGroup) {
  ExpectMatchesFind({"-gid", std::to_string(::getegid())});
}

TEST_F(ConformanceTest, UserMatchesCurrentUser) {
  const struct passwd* const pw = ::getpwuid(::geteuid());
  if (pw == nullptr) {
    GTEST_SKIP() << "no passwd entry for euid";
  }
  ExpectMatchesFind({"-user", pw->pw_name});
}

TEST_F(ConformanceTest, GroupMatchesCurrentGroup) {
  const struct group* const gr = ::getgrgid(::getegid());
  if (gr == nullptr) {
    GTEST_SKIP() << "no group entry for egid";
  }
  ExpectMatchesFind({"-group", gr->gr_name});
}

TEST_F(ConformanceTest, ParenGrouping) {
  ExpectMatchesFind({"(", "-type", "f", "-o", "-type", "d", ")"});
}

// The comma operator is a GNU extension; BSD find rejects it, so these skip on macOS.
TEST_F(ConformanceTest, CommaListImplicitPrint) {
  ExpectMatchesFind({"-name", "*.txt", ",", "-name", "*.md"});
}

TEST_F(ConformanceTest, CommaWithExplicitAction) {
  ExpectMatchesFind({"-type", "f", ",", "-print"});
}

TEST_F(ConformanceTest, PruneSkipsDirectory) {
  ExpectMatchesFind({"-name", "sub", "-prune", "-o", "-print"});
}

// -depth changes visit order only; the conformance harness sorts, so this checks
// the set is unaffected (xff's post-order property is unit-tested in walk/run).
TEST_F(ConformanceTest, DepthSameSet) {
  ExpectMatchesFind({"-depth"});
}

TEST_F(ConformanceTest, DepthWithTypeFile) {
  ExpectMatchesFind({"-depth", "-type", "f"});
}

// The fixture is single-device, so -xdev prunes nothing here; this confirms the
// option is accepted and the set is unchanged (cross-device pruning is unit-tested).
TEST_F(ConformanceTest, XdevSameSetOnSingleDevice) {
  ExpectMatchesFind({"-xdev"});
}

// -H/-L/-P are leading globals; the fixture's `link -> a.txt` makes them observable.
TEST_F(ConformanceTest, FollowAllTypeFileIncludesSymlink) {
  ExpectMatchesFind({"-L"}, {"-type", "f"});
}

TEST_F(ConformanceTest, FollowAllLeavesNoSymlinkType) {
  ExpectMatchesFind({"-L"}, {"-type", "l"});
}

TEST_F(ConformanceTest, PhysicalKeepsSymlinkType) {
  ExpectMatchesFind({"-P"}, {"-type", "l"});
}

// -regex matches the whole path; `.*\.txt` is grammar-agnostic (basic/emacs/RE2 agree).
TEST_F(ConformanceTest, RegexWholePathTxt) {
  ExpectMatchesFind({"-regex", ".*\\.txt"});
}

TEST_F(ConformanceTest, IRegexWholePathTxt) {
  ExpectMatchesFind({"-iregex", ".*\\.TXT"});
}

// -newerXY compares two files' timestamps; mtime/ctime are stable between the
// find and xff runs (atime is not, so it is avoided here). The fixture chmods
// some files, so ctime != mtime, which distinguishes the X/Y field selection.
TEST_F(ConformanceTest, NewerMtimeVsRefMtime) {
  ExpectMatchesFind({"-newermm", (root_ / "b.md").string()});
}

TEST_F(ConformanceTest, NewerCtimeVsRefMtime) {
  ExpectMatchesFind({"-newercm", (root_ / "b.md").string()});
}

TEST_F(ConformanceTest, NewerMtimeVsRefCtime) {
  ExpectMatchesFind({"-newermc", (root_ / "b.md").string()});
}

// -printf is a GNU extension; BSD find lacks it, so these skip on macOS.
TEST_F(ConformanceTest, PrintfPathAndSize) {
  ExpectMatchesFind({"-printf", "%p %s\\n"});
}

TEST_F(ConformanceTest, PrintfNameTypeDepth) {
  ExpectMatchesFind({"-printf", "%f %y %d\\n"});
}

}  // namespace
}  // namespace xff
