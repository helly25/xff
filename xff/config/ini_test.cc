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

#include "xff/config/ini.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::config {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

struct IniTest : ::testing::Test {};

TEST_F(IniTest, DefaultsRenderToCliTokens) {
  const SystemConfig c = ParseIni("[defaults]\n--color = auto\n--warn\n");
  EXPECT_THAT(c.defaults, ElementsAre("--color=auto", "--warn"));
  EXPECT_THAT(c.policy, IsEmpty());
}

TEST_F(IniTest, PolicyAllowDenyAndClassTokens) {
  const SystemConfig c = ParseIni(
      "[policy]\n"
      "project.allow = --sort, --color, --format\n"
      "project.deny  = --threads\n"
      "user.allow    = @sensitive\n");
  ASSERT_EQ(c.policy.size(), 3U);
  EXPECT_EQ(c.policy[0].layer, "project");
  EXPECT_TRUE(c.policy[0].allow);
  EXPECT_THAT(c.policy[0].tokens, ElementsAre("--sort", "--color", "--format"));
  EXPECT_EQ(c.policy[1].layer, "project");
  EXPECT_FALSE(c.policy[1].allow);
  EXPECT_THAT(c.policy[1].tokens, ElementsAre("--threads"));
  EXPECT_EQ(c.policy[2].layer, "user");
  EXPECT_TRUE(c.policy[2].allow);
  EXPECT_THAT(c.policy[2].tokens, ElementsAre("@sensitive"));
}

TEST_F(IniTest, CommentsBlanksAndBothSections) {
  const SystemConfig c =
      ParseIni("; a comment\n# another\n[defaults]\n\n--color = never\n[policy]\nproject.allow = --sort\n");
  EXPECT_THAT(c.defaults, ElementsAre("--color=never"));
  ASSERT_EQ(c.policy.size(), 1U);
  EXPECT_THAT(c.policy[0].tokens, ElementsAre("--sort"));
}

TEST_F(IniTest, MalformedPolicyLinesIgnored) {
  // No '=', no '.', and an unknown kind are each ignored (forgiving parse).
  const SystemConfig c = ParseIni("[policy]\nnonsense\nproject = x\nproject.maybe = x\n");
  EXPECT_THAT(c.policy, IsEmpty());
}

TEST_F(IniTest, LinesOutsideKnownSectionsIgnored) {
  const SystemConfig c = ParseIni("--color = auto\n[unknown]\n--foo = bar\n");
  EXPECT_THAT(c.defaults, IsEmpty());
  EXPECT_THAT(c.policy, IsEmpty());
}

}  // namespace
}  // namespace xff::config
