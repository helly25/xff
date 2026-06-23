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

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::config {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Matcher;

struct IniTest : ::testing::Test {};

// Matches a PolicyRule by its layer, allow/deny flag, and a matcher over its
// tokens, so one ElementsAre(...) covers rule count, order, and every field.
Matcher<PolicyRule> PolicyRuleIs(
    const std::string& layer,
    bool allow,
    const Matcher<std::vector<std::string>>& tokens) {
  return AllOf(
      Field("layer", &PolicyRule::layer, layer), Field("allow", &PolicyRule::allow, allow),
      Field("tokens", &PolicyRule::tokens, tokens));
}

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
  EXPECT_THAT(
      c.policy, ElementsAre(
                    PolicyRuleIs("project", true, ElementsAre("--sort", "--color", "--format")),
                    PolicyRuleIs("project", false, ElementsAre("--threads")),
                    PolicyRuleIs("user", true, ElementsAre("@sensitive"))));
}

TEST_F(IniTest, CommentsBlanksAndBothSections) {
  const SystemConfig c =
      ParseIni("; a comment\n# another\n[defaults]\n\n--color = never\n[policy]\nproject.allow = --sort\n");
  EXPECT_THAT(c.defaults, ElementsAre("--color=never"));
  EXPECT_THAT(c.policy, ElementsAre(PolicyRuleIs("project", true, ElementsAre("--sort"))));
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
