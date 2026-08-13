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

#include "xff/cli/pager.h"

#include <array>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/env/env.h"

namespace xff::cli {
namespace {

using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

struct PagerWhenTest : ::testing::Test {};

TEST_F(PagerWhenTest, AbsentResolvesToAuto) {
  EXPECT_THAT(ResolvePagerWhen({"xff", ".", "-type", "f"}), Eq(PagerWhen::kAuto));
}

TEST_F(PagerWhenTest, BarePagerIsAlways) {
  EXPECT_THAT(ResolvePagerWhen({"--pager"}), Eq(PagerWhen::kAlways));
}

TEST_F(PagerWhenTest, ExplicitValuesResolve) {
  EXPECT_THAT(ResolvePagerWhen({"--pager=always"}), Eq(PagerWhen::kAlways));
  EXPECT_THAT(ResolvePagerWhen({"--pager=never"}), Eq(PagerWhen::kNever));
  EXPECT_THAT(ResolvePagerWhen({"--pager=auto"}), Eq(PagerWhen::kAuto));
  EXPECT_THAT(ResolvePagerWhen({"--pager=all"}), Eq(PagerWhen::kAll));
}

TEST_F(PagerWhenTest, NoPagerIsNever) {
  EXPECT_THAT(ResolvePagerWhen({"--no-pager"}), Eq(PagerWhen::kNever));
}

TEST_F(PagerWhenTest, LastOccurrenceWins) {
  EXPECT_THAT(ResolvePagerWhen({"--pager=always", "--pager=never"}), Eq(PagerWhen::kNever));
  EXPECT_THAT(ResolvePagerWhen({"--no-pager", "--pager"}), Eq(PagerWhen::kAlways));
}

// Pager resolution reads XFF_PAGER / PAGER / XFF_MANPAGER through the xff/env cache; inject them
// via its test seam. Start each case from a clean, fully-unset cache so real environment values
// and prior cases do not leak in.
struct PagerCommandTest : ::testing::Test {
  void SetUp() override {
    env::ClearForTesting();
    env::SetForTesting("XFF_PAGER", std::nullopt);
    env::SetForTesting("PAGER", std::nullopt);
    env::SetForTesting("XFF_MANPAGER", std::nullopt);
  }

  void TearDown() override { env::ClearForTesting(); }
};

TEST_F(PagerCommandTest, DefaultsToLessWithColorSafeFlags) {
  EXPECT_THAT(ResolvePagerCommand(), Eq("less -FRX"));
}

TEST_F(PagerCommandTest, PagerEnvIsUsed) {
  env::SetForTesting("PAGER", "more");
  EXPECT_THAT(ResolvePagerCommand(), Eq("more"));
}

TEST_F(PagerCommandTest, XffPagerOverridesPager) {
  env::SetForTesting("PAGER", "more");
  env::SetForTesting("XFF_PAGER", "bat --paging=always");
  EXPECT_THAT(ResolvePagerCommand(), Eq("bat --paging=always"));
}

TEST_F(PagerCommandTest, EmptyEnvDisablesPaging) {
  // An explicitly-empty variable means "no pager" - it wins over the built-in default.
  env::SetForTesting("XFF_PAGER", "");
  EXPECT_THAT(ResolvePagerCommand(), Eq(""));
}

TEST_F(PagerCommandTest, ManDefaultFormatsWithMandoc) {
  // The kMan default is a roff formatter, not the plain text pager: it runs mandoc.
  EXPECT_THAT(ResolvePagerCommand(PagerKind::kMan), HasSubstr("mandoc"));
}

TEST_F(PagerCommandTest, XffManPagerOverridesTheManDefault) {
  env::SetForTesting("XFF_MANPAGER", "groff -mandoc -Tutf8 | less -R");
  EXPECT_THAT(ResolvePagerCommand(PagerKind::kMan), Eq("groff -mandoc -Tutf8 | less -R"));
}

TEST_F(PagerCommandTest, EmptyManPagerEnvDisablesManPaging) {
  env::SetForTesting("XFF_MANPAGER", "");
  EXPECT_THAT(ResolvePagerCommand(PagerKind::kMan), Eq(""));
}

TEST_F(PagerCommandTest, ManPagerIsIndependentOfTheTextPager) {
  // $XFF_PAGER selects the text pager but must not become the man command (which needs a
  // roff formatter); the man default stays mandoc-based.
  env::SetForTesting("XFF_PAGER", "most");
  EXPECT_THAT(ResolvePagerCommand(PagerKind::kText), Eq("most"));
  EXPECT_THAT(ResolvePagerCommand(PagerKind::kMan), HasSubstr("mandoc"));
}

}  // namespace

struct PagerStreamTest : ::testing::Test {
  void SetUp() override {
    env::ClearForTesting();
    env::SetForTesting("XFF_PAGER", std::nullopt);
    env::SetForTesting("PAGER", std::nullopt);
  }

  void TearDown() override { env::ClearForTesting(); }

  // Runs `body` with a PagerStream built from `when` / `stdout_is_tty` / `suppressed` and a pager
  // that writes what it reads to `sink_path`, then returns what the pager received. An INACTIVE
  // stream writes nothing there (it never redirects stdout), which is exactly the distinction the
  // tests below need.
  static std::string PagedThrough(
      PagerWhen when,
      bool stdout_is_tty,
      bool suppressed,
      const std::string& sink_path,
      const std::function<void()>& body) {
    env::SetForTesting("XFF_PAGER", absl::StrCat("cat > ", sink_path));
    {
      const PagerStream pager(when, stdout_is_tty, suppressed);
      body();
    }  // the destructor restores stdout and waits for the pager, so the file is complete here
    std::ifstream in(sink_path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }

  std::string SinkPath() const {
    return absl::StrCat(::testing::TempDir(), "/", ::testing::UnitTest::GetInstance()->current_test_info()->name());
  }
};

TEST_F(PagerStreamTest, AllPagesEverythingWrittenToStdoutDuringItsLifetime) {
  // The whole point of the streaming form: the writer does not know it is being paged, so the
  // listing needs no pager-aware code path of its own.
  EXPECT_THAT(
      PagedThrough(
          PagerWhen::kAll, /*stdout_is_tty=*/true, /*suppressed=*/false, SinkPath(), [] { std::cout << "one\ntwo\n"; }),
      Eq("one\ntwo\n"));
}

TEST_F(PagerStreamTest, EveryOtherWhenLeavesStdoutAlone) {
  // auto / always / never page the META output only; the listing is untouched by all three.
  static constexpr std::array kMetaOnly = std::to_array({PagerWhen::kAuto, PagerWhen::kAlways, PagerWhen::kNever});
  for (const PagerWhen when : kMetaOnly) {
    const std::string sink = absl::StrCat(SinkPath(), static_cast<int>(when));
    EXPECT_THAT(
        PagedThrough(when, /*stdout_is_tty=*/true, /*suppressed=*/false, sink, [] { std::cout << "x" << std::flush; }),
        IsEmpty());
  }
}

TEST_F(PagerStreamTest, NoTerminalMeansNoPager) {
  // A listing forced through a pager in a pipeline would hand the pager's screen handling to the
  // next command, so kAll is terminal-only (unlike kAlways for meta output).
  EXPECT_THAT(
      PagedThrough(
          PagerWhen::kAll, /*stdout_is_tty=*/false, /*suppressed=*/false, SinkPath(),
          [] { std::cout << "x" << std::flush; }),
      IsEmpty());
}

TEST_F(PagerStreamTest, SuppressedMeansNoPager) {
  // The caller's veto for -ok / -exec and --quiet: the pager must not sit between the primary and
  // the user.
  EXPECT_THAT(
      PagedThrough(
          PagerWhen::kAll, /*stdout_is_tty=*/true, /*suppressed=*/true, SinkPath(),
          [] { std::cout << "x" << std::flush; }),
      IsEmpty());
}

TEST_F(PagerStreamTest, AnEmptyPagerVariableDisablesPagingRatherThanLosingOutput) {
  // "$XFF_PAGER set to empty means no pager" is the contract EmitPaged has; the streaming form
  // must not differ, and must leave stdout usable.
  env::SetForTesting("XFF_PAGER", "");
  const PagerStream pager(PagerWhen::kAll, /*stdout_is_tty=*/true, /*suppressed=*/false);
  EXPECT_THAT(pager.active(), false);
}

TEST_F(PagerStreamTest, FinishIsIdempotentAndRestoresStdout) {
  const std::string sink = SinkPath();
  env::SetForTesting("XFF_PAGER", absl::StrCat("cat > ", sink));
  PagerStream pager(PagerWhen::kAll, /*stdout_is_tty=*/true, /*suppressed=*/false);
  EXPECT_THAT(pager.active(), true);
  std::cout << "paged\n";
  pager.Finish();
  EXPECT_THAT(pager.active(), false);
  pager.Finish();  // a second call (and the destructor after it) must be harmless
  std::ifstream in(sink);
  EXPECT_THAT(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()), Eq("paged\n"));
}

}  // namespace xff::cli
