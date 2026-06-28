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

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/cli/help.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::HasSubstr;

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

TEST_F(HelpTest, ListAndAllAreIndexAliases) {
  // `--help=list` / `--help=all` render the same index as the empty topic.
  const absl::StatusOr<std::string> index = RenderHelp("");
  ASSERT_THAT(index, IsOk());
  EXPECT_THAT(RenderHelp("list"), IsOkAndHolds(*index));
  EXPECT_THAT(RenderHelp("all"), IsOkAndHolds(*index));
}

}  // namespace
}  // namespace xff::cli
