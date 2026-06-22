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

#include "xff/config/xffrc.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::config {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

struct XffrcTest : ::testing::Test {};

TEST_F(XffrcTest, SkipsBlanksAndComments) {
  EXPECT_THAT(ParseXffrc("\n  \n# a comment\n   # indented comment\n"), IsEmpty());
}

TEST_F(XffrcTest, BareFlagsAreCommonAnyConfig) {
  const auto lines = ParseXffrc("--color=auto --sort");
  ASSERT_EQ(lines.size(), 1U);
  EXPECT_EQ(lines[0].base, "");  // no selector -> common
  EXPECT_EQ(lines[0].config, "");
  EXPECT_THAT(lines[0].flags, ElementsAre("--color=auto", "--sort"));
}

TEST_F(XffrcTest, BaseSelector) {
  const auto lines = ParseXffrc("xff: --feature=long-paths");
  ASSERT_EQ(lines.size(), 1U);
  EXPECT_EQ(lines[0].base, "xff");
  EXPECT_EQ(lines[0].config, "");
  EXPECT_THAT(lines[0].flags, ElementsAre("--feature=long-paths"));
}

TEST_F(XffrcTest, BaseAndConfigSelector) {
  const auto lines = ParseXffrc("xff:debug: --feature=trace --threads=1");
  ASSERT_EQ(lines.size(), 1U);
  EXPECT_EQ(lines[0].base, "xff");
  EXPECT_EQ(lines[0].config, "debug");
  EXPECT_THAT(lines[0].flags, ElementsAre("--feature=trace", "--threads=1"));
}

TEST_F(XffrcTest, CommonSelectorPreservedVerbatim) {
  const auto lines = ParseXffrc("common: --color=auto");
  ASSERT_EQ(lines.size(), 1U);
  EXPECT_EQ(lines[0].base, "common");  // the loader treats "common" == ""
  EXPECT_THAT(lines[0].flags, ElementsAre("--color=auto"));
}

TEST_F(XffrcTest, SelectorOnlyLineHasNoFlags) {
  const auto lines = ParseXffrc("find: --warn\nxff:\nxff: --feature=x");
  ASSERT_EQ(lines.size(), 3U);
  EXPECT_EQ(lines[0].base, "find");
  EXPECT_THAT(lines[0].flags, ElementsAre("--warn"));
  EXPECT_EQ(lines[1].base, "xff");  // selector with no flags
  EXPECT_THAT(lines[1].flags, IsEmpty());
  EXPECT_EQ(lines[2].base, "xff");
  EXPECT_THAT(lines[2].flags, ElementsAre("--feature=x"));
}

TEST_F(XffrcTest, FlagValueWithColonIsNotASelector) {
  const auto lines = ParseXffrc("--config=xff:2");  // ends in '2', not ':'
  ASSERT_EQ(lines.size(), 1U);
  EXPECT_EQ(lines[0].base, "");
  EXPECT_THAT(lines[0].flags, ElementsAre("--config=xff:2"));
}

}  // namespace
}  // namespace xff::config
