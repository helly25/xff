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

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/cli/help.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

using ::testing::HasSubstr;

struct HelpTest : ::testing::Test {};

TEST_F(HelpTest, TopicRendersNameSummaryAndTags) {
  const HelpResult result = RenderHelp("-regex");
  EXPECT_TRUE(result.found);
  EXPECT_THAT(result.text, HasSubstr("-regex"));
  EXPECT_THAT(result.text, HasSubstr("regular expression"));  // the summary
  EXPECT_THAT(result.text, HasSubstr("test"));                // kind tag
  EXPECT_THAT(result.text, HasSubstr("find"));                // style tag
}

TEST_F(HelpTest, DashlessTopicResolves) {
  // Friendly: `xff help regex` finds -regex.
  const HelpResult result = RenderHelp("regex");
  EXPECT_TRUE(result.found);
  EXPECT_THAT(result.text, HasSubstr("-regex"));
}

TEST_F(HelpTest, XffOperatorIsTaggedXff) {
  const HelpResult result = RenderHelp("-xor");
  EXPECT_TRUE(result.found);
  EXPECT_THAT(result.text, HasSubstr("operator"));
  EXPECT_THAT(result.text, HasSubstr("xff"));
}

TEST_F(HelpTest, SecurityActionIsTagged) {
  EXPECT_THAT(RenderHelp("-exec").text, HasSubstr("runs commands"));
}

TEST_F(HelpTest, VariadicArgHintShowsCommandForm) {
  EXPECT_THAT(RenderHelp("-exec").text, HasSubstr("CMD..."));
}

TEST_F(HelpTest, UnknownTopicIsNotFound) {
  const HelpResult result = RenderHelp("-bogus");
  EXPECT_FALSE(result.found);
  EXPECT_THAT(result.text, HasSubstr("no help topic"));
}

TEST_F(HelpTest, IndexGroupsByKindAndListsEveryDescriptor) {
  const HelpResult result = RenderHelp("");
  EXPECT_TRUE(result.found);
  EXPECT_THAT(result.text, HasSubstr("Tests:"));
  EXPECT_THAT(result.text, HasSubstr("Actions:"));
  EXPECT_THAT(result.text, HasSubstr("Operators:"));
  for (const registry::Descriptor& descriptor : registry::All()) {
    EXPECT_THAT(result.text, HasSubstr(descriptor.name)) << descriptor.name;
  }
}

TEST_F(HelpTest, ListAndAllAreIndexAliases) {
  // `--help=list` / `--help=all` render the same index as the empty topic.
  const HelpResult index = RenderHelp("");
  EXPECT_EQ(RenderHelp("list").text, index.text);
  EXPECT_EQ(RenderHelp("all").text, index.text);
}

}  // namespace
}  // namespace xff::cli
