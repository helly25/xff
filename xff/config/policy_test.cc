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

#include "xff/config/policy.h"

#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/config/config.h"
#include "xff/config/ini.h"
#include "xff/config/xffrc.h"
#include "xff/registry/descriptor.h"

namespace xff::config {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::SizeIs;

struct PolicyTest : ::testing::Test {};

RcLine Line(std::vector<std::string> flags) {
  return RcLine{.flags = std::move(flags)};
}

TEST_F(PolicyTest, LineSafetyTakesTheWorstFlag) {
  EXPECT_THAT(LineSafety(Line({"--color=auto"})), registry::Safety::kNone);
  EXPECT_THAT(LineSafety(Line({"-exec", "rm", ";"})), registry::Safety::kSecurity);
  EXPECT_THAT(LineSafety(Line({"-delete"})), registry::Safety::kSafety);
  EXPECT_THAT(LineSafety(Line({"-capture=tag", "cmd", ";"})), registry::Safety::kSecurity);        // base before '='
  EXPECT_THAT(LineSafety(Line({"-name", "x", "-exec", "rm", ";"})), registry::Safety::kSecurity);  // worst wins
}

TEST_F(PolicyTest, BuiltInDeniesProjectSensitiveAndDestructive) {
  const SystemConfig none;  // no [policy]
  EXPECT_FALSE(LinePermitted(Line({"-exec", "rm", ";"}), Source::kProject, none));
  EXPECT_FALSE(LinePermitted(Line({"-delete"}), Source::kProject, none));
  EXPECT_TRUE(LinePermitted(Line({"--color=auto"}), Source::kProject, none));   // safe is fine
  EXPECT_TRUE(LinePermitted(Line({"-exec", "rm", ";"}), Source::kUser, none));  // user may arm
  EXPECT_TRUE(LinePermitted(Line({"-delete"}), Source::kSystem, none));         // system may
}

TEST_F(PolicyTest, PolicyAllowLoosensByFlagName) {
  SystemConfig policy;
  policy.policy = {PolicyRule{.layer = "project", .allow = true, .tokens = {"-capture"}}};
  EXPECT_TRUE(LinePermitted(Line({"-capture=w", "cmd", ";"}), Source::kProject, policy));  // armed by name
  EXPECT_FALSE(LinePermitted(Line({"-exec", "rm", ";"}), Source::kProject, policy));       // -exec still denied
}

TEST_F(PolicyTest, PolicyAllowLoosensByClassToken) {
  SystemConfig policy;
  policy.policy = {PolicyRule{.layer = "project", .allow = true, .tokens = {"@sensitive"}}};
  EXPECT_TRUE(LinePermitted(Line({"-exec", "rm", ";"}), Source::kProject, policy));  // @sensitive armed
  EXPECT_FALSE(LinePermitted(Line({"-delete"}), Source::kProject, policy));          // @destructive still denied
}

TEST_F(PolicyTest, PolicyDenyTightensASafeFlag) {
  SystemConfig policy;
  policy.policy = {PolicyRule{.layer = "project", .allow = false, .tokens = {"--threads"}}};
  EXPECT_FALSE(LinePermitted(Line({"--threads=4"}), Source::kProject, policy));  // safe but tightened
  EXPECT_TRUE(LinePermitted(Line({"--color=auto"}), Source::kProject, policy));  // unrelated safe flag fine
}

TEST_F(PolicyTest, DenyBeatsAllowOnConflict) {
  SystemConfig policy;
  policy.policy = {
      PolicyRule{.layer = "project", .allow = true, .tokens = {"-exec"}},
      PolicyRule{.layer = "project", .allow = false, .tokens = {"-exec"}},
  };
  EXPECT_FALSE(LinePermitted(Line({"-exec", "rm", ";"}), Source::kProject, policy));
}

TEST_F(PolicyTest, PolicyRulesAreScopedToTheirLayer) {
  SystemConfig policy;
  policy.policy = {PolicyRule{.layer = "user", .allow = false, .tokens = {"-exec"}}};
  // The user.deny tightens the user layer (normally allowed)...
  EXPECT_FALSE(LinePermitted(Line({"-exec", "rm", ";"}), Source::kUser, policy));
  // ...but does not touch the project layer (still its built-in deny).
  EXPECT_FALSE(LinePermitted(Line({"-exec", "rm", ";"}), Source::kProject, policy));
}

TEST_F(PolicyTest, GateConfigDropsDeniedProjectLinesAndRecordsThem) {
  ConfigInputs inputs;
  inputs.user = {Line({"-exec", "rm", ";"}), Line({"--color=auto"})};      // user: both allowed
  inputs.project = {Line({"-exec", "rm", ";"}), Line({"--color=never"})};  // project: -exec denied
  std::vector<Drop> drops;
  const ConfigInputs gated = GateConfig(inputs, &drops);
  EXPECT_THAT(gated.user, SizeIs(2));  // the user layer keeps everything
  ASSERT_THAT(gated.project, SizeIs(1));
  EXPECT_THAT(gated.project.front().flags, ElementsAre("--color=never"));  // only the safe project line survives
  ASSERT_THAT(drops, SizeIs(1));
  EXPECT_THAT(drops.front().layer, Source::kProject);
  EXPECT_THAT(drops.front().safety, registry::Safety::kSecurity);
  EXPECT_THAT(drops.front().line.flags, ElementsAre("-exec", "rm", ";"));
}

TEST_F(PolicyTest, GateConfigToleratesNullDropsSink) {
  ConfigInputs inputs;
  inputs.project = {Line({"-delete"})};
  EXPECT_THAT(GateConfig(inputs, nullptr).project, IsEmpty());  // denied, dropped, no crash
}

TEST_F(PolicyTest, DropMessageNamesPrimaryLayerAndClass) {
  const Drop drop{
      .line = Line({"-exec", "rm", ";"}),
      .layer = Source::kProject,
      .safety = registry::Safety::kSecurity,
  };
  EXPECT_THAT(DropMessage(drop), "'-exec' from the project .xffrc (sensitive)");
}

}  // namespace
}  // namespace xff::config
