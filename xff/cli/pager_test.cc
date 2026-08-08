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

#include <cstdlib>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::cli {
namespace {

using ::testing::Eq;
using ::testing::HasSubstr;

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
}

TEST_F(PagerWhenTest, NoPagerIsNever) {
  EXPECT_THAT(ResolvePagerWhen({"--no-pager"}), Eq(PagerWhen::kNever));
}

TEST_F(PagerWhenTest, LastOccurrenceWins) {
  EXPECT_THAT(ResolvePagerWhen({"--pager=always", "--pager=never"}), Eq(PagerWhen::kNever));
  EXPECT_THAT(ResolvePagerWhen({"--no-pager", "--pager"}), Eq(PagerWhen::kAlways));
}

// Mutates XFF_PAGER / PAGER in the environment; restores them on teardown so the cases
// do not leak into one another.
struct PagerCommandTest : ::testing::Test {
  void SetUp() override {
    Save("XFF_PAGER", saved_xff_, has_xff_);
    Save("PAGER", saved_pager_, has_pager_);
    Save("XFF_MANPAGER", saved_manpager_, has_manpager_);
    ::unsetenv("XFF_PAGER");
    ::unsetenv("PAGER");
    ::unsetenv("XFF_MANPAGER");
  }

  void TearDown() override {
    Restore("XFF_PAGER", saved_xff_, has_xff_);
    Restore("PAGER", saved_pager_, has_pager_);
    Restore("XFF_MANPAGER", saved_manpager_, has_manpager_);
  }

  static void Save(const char* name, std::string& into, bool& present) {
    const char* value = std::getenv(name);
    present = value != nullptr;
    if (present) {
      into = value;
    }
  }

  static void Restore(const char* name, const std::string& value, bool present) {
    if (present) {
      ::setenv(name, value.c_str(), 1);
    } else {
      ::unsetenv(name);
    }
  }

  std::string saved_xff_;
  std::string saved_pager_;
  std::string saved_manpager_;
  bool has_xff_ = false;
  bool has_pager_ = false;
  bool has_manpager_ = false;
};

TEST_F(PagerCommandTest, DefaultsToLessWithColorSafeFlags) {
  EXPECT_THAT(ResolvePagerCommand(), Eq("less -FRX"));
}

TEST_F(PagerCommandTest, PagerEnvIsUsed) {
  ::setenv("PAGER", "more", 1);
  EXPECT_THAT(ResolvePagerCommand(), Eq("more"));
}

TEST_F(PagerCommandTest, XffPagerOverridesPager) {
  ::setenv("PAGER", "more", 1);
  ::setenv("XFF_PAGER", "bat --paging=always", 1);
  EXPECT_THAT(ResolvePagerCommand(), Eq("bat --paging=always"));
}

TEST_F(PagerCommandTest, EmptyEnvDisablesPaging) {
  // An explicitly-empty variable means "no pager" - it wins over the built-in default.
  ::setenv("XFF_PAGER", "", 1);
  EXPECT_THAT(ResolvePagerCommand(), Eq(""));
}

TEST_F(PagerCommandTest, ManDefaultFormatsWithMandoc) {
  // The kMan default is a roff formatter, not the plain text pager: it runs mandoc.
  EXPECT_THAT(ResolvePagerCommand(PagerKind::kMan), HasSubstr("mandoc"));
}

TEST_F(PagerCommandTest, XffManPagerOverridesTheManDefault) {
  ::setenv("XFF_MANPAGER", "groff -mandoc -Tutf8 | less -R", 1);
  EXPECT_THAT(ResolvePagerCommand(PagerKind::kMan), Eq("groff -mandoc -Tutf8 | less -R"));
}

TEST_F(PagerCommandTest, EmptyManPagerEnvDisablesManPaging) {
  ::setenv("XFF_MANPAGER", "", 1);
  EXPECT_THAT(ResolvePagerCommand(PagerKind::kMan), Eq(""));
}

TEST_F(PagerCommandTest, ManPagerIsIndependentOfTheTextPager) {
  // $XFF_PAGER selects the text pager but must not become the man command (which needs a
  // roff formatter); the man default stays mandoc-based.
  ::setenv("XFF_PAGER", "most", 1);
  EXPECT_THAT(ResolvePagerCommand(PagerKind::kText), Eq("most"));
  EXPECT_THAT(ResolvePagerCommand(PagerKind::kMan), HasSubstr("mandoc"));
}

}  // namespace
}  // namespace xff::cli
