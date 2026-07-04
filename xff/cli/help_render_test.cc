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

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/cli/globals.h"
#include "xff/cli/help.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::EndsWith;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Lt;
using ::testing::Not;
using ::testing::SizeIs;

struct HelpTest : ::testing::Test {};

TEST_F(HelpTest, TopicRendersNameSummaryAndTags) {
  EXPECT_THAT(
      RenderHelp("-regex"), IsOkAndHolds(AllOf(
                                HasSubstr("-regex"), HasSubstr("regular expression"),  // the summary
                                HasSubstr("test"), HasSubstr("find"))));               // kind + style tags
}

TEST_F(HelpTest, DashlessTopicResolves) {
  // Friendly: `--help=regex` finds -regex.
  EXPECT_THAT(RenderHelp("regex"), IsOkAndHolds(HasSubstr("-regex")));
}

TEST_F(HelpTest, XffOperatorIsTaggedXff) {
  EXPECT_THAT(RenderHelp("-xor"), IsOkAndHolds(AllOf(HasSubstr("operator"), HasSubstr("xff"))));
}

TEST_F(HelpTest, SecurityActionIsTagged) {
  EXPECT_THAT(RenderHelp("-exec"), IsOkAndHolds(HasSubstr("runs commands")));
}

TEST_F(HelpTest, VariadicArgHintShowsCommandForm) {
  EXPECT_THAT(RenderHelp("-exec"), IsOkAndHolds(HasSubstr("CMD...")));
}

TEST_F(HelpTest, UnknownTopicIsNotFound) {
  // RenderHelp signals unknown-topic with the status code only; the user-facing
  // message is the caller's to compose (verified end to end in help_topic_test.sh).
  EXPECT_THAT(RenderHelp("-bogus"), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(HelpTest, IndexGroupsByKindAndListsEveryDescriptor) {
  const absl::StatusOr<std::string> index = RenderHelp("");
  ASSERT_THAT(index, IsOk());
  EXPECT_THAT(*index, AllOf(HasSubstr("Tests:"), HasSubstr("Actions:"), HasSubstr("Operators:")));
  for (const registry::Descriptor& descriptor : registry::All()) {
    EXPECT_THAT(*index, HasSubstr(descriptor.name)) << descriptor.name;
  }
}

TEST_F(HelpTest, ListIsAnIndexAliasEndingWithTheTopicMap) {
  // `--help=list` renders the same index as the empty topic, and the index ends with
  // the generated help-topic map (so `--help=list` is also the help-system map).
  const absl::StatusOr<std::string> index = RenderHelp("");
  ASSERT_THAT(index, IsOk());
  EXPECT_THAT(RenderHelp("list"), IsOkAndHolds(*index));
  EXPECT_THAT(*index, HasSubstr("Help topics"));
}

TEST_F(HelpTest, FullIsDetailedAndAllIsShort) {
  // `full` (alias long) carries the long per-flag explanations; `all` is the same set
  // (every option + primary) but summaries only -- strictly shorter, no detail prose.
  const absl::StatusOr<std::string> full = RenderHelp("full");
  const absl::StatusOr<std::string> all = RenderHelp("all");
  ASSERT_THAT(full, IsOk());
  ASSERT_THAT(all, IsOk());
  EXPECT_THAT(*full, AllOf(HasSubstr("--sort"), HasSubstr("-regex"), HasSubstr("EXPRESSION")));
  EXPECT_THAT(*all, AllOf(HasSubstr("--sort"), HasSubstr("-regex")));
  EXPECT_THAT(RenderHelp("long"), IsOkAndHolds(*full));           // long is a synonym of full
  EXPECT_THAT(*full, HasSubstr("A config style sets"));           // a seeded --config detail paragraph
  EXPECT_THAT(*all, Not(HasSubstr("A config style sets")));       // all omits the long explanations
  EXPECT_THAT(*all, SizeIs(Lt(full->size())));                    // strictly shorter than full
  EXPECT_THAT(RenderHelp("list"), IsOkAndHolds(Not(Eq(*full))));  // and neither is the terse index
}

TEST_F(HelpTest, SingleFlagHelpShowsTheLongExplanation) {
  // `--help=NAME` for a flag with a `details` paragraph shows it (not just the summary).
  EXPECT_THAT(RenderHelp("--config"), IsOkAndHolds(HasSubstr("A config style sets")));
  EXPECT_THAT(RenderHelp("--time-format"), IsOkAndHolds(HasSubstr("per-field qualifier")));
}

TEST_F(HelpTest, HelpSectionIsGeneratedFromFlagsAndTopics) {
  // The usage-page Help: section is built from HelpFlags() + the topic index, not a
  // hand-written string. It shows the meta/doc flags and nests the topic list.
  EXPECT_THAT(HelpFlags(), Not(IsEmpty()));
  const std::string section = RenderHelpSection();
  EXPECT_THAT(section, AllOf(HasSubstr("--help=TOPIC"), HasSubstr("--man"), HasSubstr("--version")));
  EXPECT_THAT(section, HasSubstr("fields"));  // the topic index (HelpTopics) is nested in
}

TEST_F(HelpTest, HelpGuideListsEveryTopic) {
  // `--help=help` is the help-system guide; it lists every topic from the SOT.
  const absl::StatusOr<std::string> guide = RenderHelp("help");
  ASSERT_THAT(guide, IsOk());
  EXPECT_THAT(*guide, HasSubstr("Topics"));
  for (const HelpTopic& topic : HelpTopics()) {
    EXPECT_THAT(*guide, HasSubstr(topic.name)) << topic.name;
  }
}

TEST_F(HelpTest, EveryAdvertisedTopicRendersAndAliasesAreSynonyms) {
  // Drift guard: every advertised topic resolves to substantial, newline-terminated
  // help, and each alias is a pure synonym (byte-identical output). Only `styles` /
  // `flavors` are excluded -- the CLI renders those (they need the engine).
  for (const HelpTopic& topic : HelpTopics()) {
    if (topic.name == "styles") {
      continue;
    }
    const absl::StatusOr<std::string> rendered = RenderHelp(topic.name);
    ASSERT_THAT(rendered, IsOk()) << topic.name;
    EXPECT_THAT(*rendered, AllOf(SizeIs(Gt(10)), EndsWith("\n"))) << topic.name;
    for (const std::string_view alias : topic.aliases) {
      EXPECT_THAT(RenderHelp(alias), IsOkAndHolds(*rendered)) << alias;
    }
  }
}

TEST_F(HelpTest, ExpressionsListsEveryPrimaryGroupedWithoutGlobals) {
  // `--help=expressions` is the annotated Tests/Actions/Operators list -- every
  // expression primary with its summary, but not the whole-run global flags.
  const absl::StatusOr<std::string> expr = RenderHelp("expressions");
  ASSERT_THAT(expr, IsOk());
  EXPECT_THAT(*expr, AllOf(HasSubstr("Tests:"), HasSubstr("Actions:"), HasSubstr("Operators:")));
  for (const registry::Descriptor& descriptor : registry::All()) {
    EXPECT_THAT(*expr, HasSubstr(descriptor.name)) << descriptor.name;
  }
  // The global-flag groups from the full index are absent (e.g. the "Traversal:" header).
  EXPECT_THAT(*expr, Not(HasSubstr("Traversal:")));
}

TEST_F(HelpTest, GlobalFlagTopicRendersWithGlobalTag) {
  EXPECT_THAT(
      RenderHelp("--sort"), IsOkAndHolds(AllOf(HasSubstr("--sort"), HasSubstr("ordering"), HasSubstr("global"))));
}

TEST_F(HelpTest, GlobalFlagResolvesByAliasAndDashless) {
  EXPECT_THAT(RenderHelp("-j"), IsOkAndHolds(HasSubstr("--jobs")));    // alias -> --jobs
  EXPECT_THAT(RenderHelp("sort"), IsOkAndHolds(HasSubstr("--sort")));  // dash-less -> --sort
}

TEST_F(HelpTest, IndexIncludesGlobalGroupsAndEveryFlag) {
  const absl::StatusOr<std::string> index = RenderHelp("");
  ASSERT_THAT(index, IsOk());
  EXPECT_THAT(*index, AllOf(HasSubstr("Config:"), HasSubstr("Traversal:")));
  for (const GlobalFlag& flag : Globals()) {
    EXPECT_THAT(*index, HasSubstr(flag.name)) << flag.name;
  }
}

}  // namespace
}  // namespace xff::cli
