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
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/config/xffrc.h"
#include "xff/registry/descriptor.h"

namespace xff::config {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

struct ConfigTest : ::testing::Test {};

// Matches a ResolvedFlag by both its flag text and its provenance, so an
// ElementsAre(...) assertion folds size, order, flag, and Source into one check.
testing::Matcher<ResolvedFlag> FlagIs(const std::string& flag, Source source) {
  return AllOf(Field("flag", &ResolvedFlag::flag, flag), Field("source", &ResolvedFlag::source, source));
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

TEST_F(ConfigTest, SourceNameMapsEachLayer) {
  EXPECT_THAT(SourceName(Source::kSystem), "system");
  EXPECT_THAT(SourceName(Source::kUser), "user");
  EXPECT_THAT(SourceName(Source::kProject), "project");
  EXPECT_THAT(SourceName(Source::kCli), "cli");
  EXPECT_THAT(SourceName(Source::kUnset), "unset");
}

TEST_F(ConfigTest, ActiveStyleDefaultsToXffAndTracksTheConfigStack) {
  EXPECT_THAT(ActiveStyle({}), registry::Style::kXff);         // no selector -> the modern default
  EXPECT_THAT(ActiveStyle({"find"}), registry::Style::kFind);  // strict find
  EXPECT_THAT(ActiveStyle({"xff"}), registry::Style::kXff);
  EXPECT_THAT(ActiveStyle({"debug"}), registry::Style::kXff);           // a custom config name is not a style
  EXPECT_THAT(ActiveStyle({"xff:2"}), registry::Style::kXff);           // version-pinned epoch -> base "xff"
  EXPECT_THAT(ActiveStyle({"find", "debug"}), registry::Style::kFind);  // a custom config keeps the style
  EXPECT_THAT(ActiveStyle({"xff", "find"}), registry::Style::kFind);    // the last style selector wins
  EXPECT_THAT(ActiveStyle({"find", "xff"}), registry::Style::kXff);
}

TEST_F(ConfigTest, DefaultStyleForProgramSelectsByBasename) {
  EXPECT_THAT(DefaultStyleForProgram("find"), "find");
  EXPECT_THAT(DefaultStyleForProgram("/usr/local/bin/find"), "find");  // the basename, not the path
  EXPECT_THAT(DefaultStyleForProgram("./find"), "find");
  EXPECT_THAT(DefaultStyleForProgram("xff"), "xff");
  EXPECT_THAT(DefaultStyleForProgram("/opt/helly25/xff"), "xff");
  EXPECT_THAT(DefaultStyleForProgram("myfind"), "xff");     // only the exact name "find" is strict
  EXPECT_THAT(DefaultStyleForProgram("findutils"), "xff");  // not a prefix/substring match
  EXPECT_THAT(DefaultStyleForProgram(""), "xff");
}

TEST_F(ConfigTest, ExplainConfigTagsEachFlagWithProvenance) {
  const std::vector<ResolvedFlag> resolved = {
      {.flag = "--color=auto", .source = Source::kSystem}, {.flag = "--sort", .source = Source::kUser}};
  const std::string explained = ExplainConfig(resolved, {"--format=jsonl"});
  EXPECT_THAT(explained, HasSubstr("system\t--color=auto\n"));
  EXPECT_THAT(explained, HasSubstr("user\t--sort\n"));
  EXPECT_THAT(explained, HasSubstr("cli\t--format=jsonl\n"));
}

}  // namespace
}  // namespace xff::config
