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

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/config/xffrc.h"

namespace xff::config {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;

struct ConfigTest : ::testing::Test {};

// Matches a ResolvedFlag by both its flag text and its provenance, so an
// ElementsAre(...) assertion folds size, order, flag, and Source into one check.
testing::Matcher<ResolvedFlag> FlagIs(const std::string& flag, Source source) {
  return AllOf(Field(&ResolvedFlag::flag, flag), Field(&ResolvedFlag::source, source));
}

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
  EXPECT_THAT(
      ResolveConfig(in), ElementsAre(FlagIs("--color=auto", Source::kSystem), FlagIs("--threads=4", Source::kSystem)));
}

TEST_F(ConfigTest, CommonAndBareLinesAlwaysApply) {
  ConfigInputs in;
  in.user = ParseXffrc("common: --color=never\n--sort");
  EXPECT_THAT(ResolveConfig(in), ElementsAre(FlagIs("--color=never", Source::kUser), FlagIs("--sort", Source::kUser)));
}

TEST_F(ConfigTest, BaseSelectorGatedByActiveConfig) {
  ConfigInputs in;
  in.user = ParseXffrc("xff: --feature=long-paths\nfind: --warn");
  EXPECT_THAT(ResolveConfig(in), IsEmpty());  // no active --config -> neither base applies
  in.configs = {"xff"};
  EXPECT_THAT(ResolveConfig(in), ElementsAre(FlagIs("--feature=long-paths", Source::kUser)));
}

TEST_F(ConfigTest, ConfigSelectorGatedByNamedConfig) {
  ConfigInputs in;
  in.user = ParseXffrc("xff:debug: --threads=1");
  in.configs = {"xff"};  // style active, but not the :debug named config
  EXPECT_THAT(ResolveConfig(in), IsEmpty());
  in.configs = {"xff", "debug"};
  EXPECT_THAT(ResolveConfig(in), ElementsAre(FlagIs("--threads=1", Source::kUser)));
}

TEST_F(ConfigTest, LayerPrecedenceSystemThenUserThenProject) {
  ConfigInputs in;
  in.system.defaults = {"--color=auto"};
  in.user = ParseXffrc("common: --sort");
  in.project = ParseXffrc("common: --color=never");
  EXPECT_THAT(
      ResolveConfig(in), ElementsAre(
                             FlagIs("--color=auto", Source::kSystem), FlagIs("--sort", Source::kUser),
                             FlagIs("--color=never", Source::kProject)));
}

}  // namespace
}  // namespace xff::config
