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

#include "xff/cli/markdown.h"

#include <string>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/cli/globals.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

using ::testing::AllOf;
using ::testing::HasSubstr;

struct MarkdownTest : ::testing::Test {};

TEST_F(MarkdownTest, HasTitleAndSectionHeadings) {
  EXPECT_THAT(
      MarkdownReference(), AllOf(
                               HasSubstr("# xff"), HasSubstr("## Options"), HasSubstr("### Config"),
                               HasSubstr("## Expression"), HasSubstr("### Tests"), HasSubstr("### Operators")));
}

TEST_F(MarkdownTest, DocumentsEveryGlobalAndPrimaryAsCode) {
  const std::string doc = MarkdownReference();
  for (const GlobalFlag& flag : Globals()) {
    EXPECT_THAT(doc, HasSubstr(absl::StrCat("`", flag.display, "`"))) << flag.name;
  }
  for (const registry::Descriptor& descriptor : registry::All()) {
    EXPECT_THAT(doc, HasSubstr(absl::StrCat("`", descriptor.name))) << descriptor.name;
  }
}

TEST_F(MarkdownTest, TagsXffExtensions) {
  EXPECT_THAT(MarkdownReference(), HasSubstr("_(xff)_"));
}

}  // namespace
}  // namespace xff::cli
