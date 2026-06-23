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

#include "xff/config/config.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/config/xffrc.h"

namespace xff::config {
namespace {

using ::testing::IsEmpty;

struct ConfigTest : ::testing::Test {};

TEST_F(ConfigTest, NoConfigYieldsEmpty) {
  ConfigInputs in;
  in.system.defaults = {"--color=auto"};
  in.user = ParseXffrc("common: --sort");
  in.no_config = true;
  EXPECT_THAT(ResolveConfig(in), IsEmpty());
}

TEST_F(ConfigTest, SystemDefaultsAreLowestPrecedence) {
  ConfigInputs in;
  in.system.defaults = {"--color=auto", "--threads=4"};
  const auto resolved = ResolveConfig(in);
  ASSERT_EQ(resolved.size(), 2U);
  EXPECT_EQ(resolved[0].flag, "--color=auto");
  EXPECT_EQ(resolved[0].source, Source::kSystem);
  EXPECT_EQ(resolved[1].flag, "--threads=4");
}

TEST_F(ConfigTest, CommonAndBareLinesAlwaysApply) {
  ConfigInputs in;
  in.user = ParseXffrc("common: --color=never\n--sort");
  const auto resolved = ResolveConfig(in);
  ASSERT_EQ(resolved.size(), 2U);
  EXPECT_EQ(resolved[0].flag, "--color=never");
  EXPECT_EQ(resolved[0].source, Source::kUser);
  EXPECT_EQ(resolved[1].flag, "--sort");
}

TEST_F(ConfigTest, BaseSelectorGatedByActiveConfig) {
  ConfigInputs in;
  in.user = ParseXffrc("xff: --feature=long-paths\nfind: --warn");
  EXPECT_THAT(ResolveConfig(in), IsEmpty());  // no active --config -> neither base applies
  in.configs = {"xff"};
  const auto resolved = ResolveConfig(in);
  ASSERT_EQ(resolved.size(), 1U);
  EXPECT_EQ(resolved[0].flag, "--feature=long-paths");
}

TEST_F(ConfigTest, ConfigSelectorGatedByNamedConfig) {
  ConfigInputs in;
  in.user = ParseXffrc("xff:debug: --threads=1");
  in.configs = {"xff"};  // style active, but not the :debug named config
  EXPECT_THAT(ResolveConfig(in), IsEmpty());
  in.configs = {"xff", "debug"};
  const auto resolved = ResolveConfig(in);
  ASSERT_EQ(resolved.size(), 1U);
  EXPECT_EQ(resolved[0].flag, "--threads=1");
}

TEST_F(ConfigTest, LayerPrecedenceSystemThenUserThenProject) {
  ConfigInputs in;
  in.system.defaults = {"--color=auto"};
  in.user = ParseXffrc("common: --sort");
  in.project = ParseXffrc("common: --color=never");
  const auto resolved = ResolveConfig(in);
  ASSERT_EQ(resolved.size(), 3U);
  EXPECT_EQ(resolved[0].source, Source::kSystem);   // --color=auto
  EXPECT_EQ(resolved[1].source, Source::kUser);     // --sort
  EXPECT_EQ(resolved[2].source, Source::kProject);  // --color=never (later wins when applied)
}

}  // namespace
}  // namespace xff::config
