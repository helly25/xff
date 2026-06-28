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

#include "xff/cli/manpage.h"

#include <string>

#include "absl/strings/str_replace.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/cli/globals.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

using ::testing::AllOf;
using ::testing::HasSubstr;

// Undo the roff hyphen/backslash escaping so assertions can match plain names.
std::string Plain(const std::string& page) {
  return absl::StrReplaceAll(page, {{"\\-", "-"}, {"\\\\", "\\"}});
}

struct ManPageTest : ::testing::Test {};

TEST_F(ManPageTest, HasTheStandardManSections) {
  const std::string page = ManPage();
  EXPECT_THAT(
      page, AllOf(
                HasSubstr(".TH xff 1"), HasSubstr(".SH NAME"), HasSubstr(".SH SYNOPSIS"), HasSubstr(".SH DESCRIPTION"),
                HasSubstr(".SH OPTIONS"), HasSubstr(".SH EXPRESSION"), HasSubstr(".SH EXIT STATUS"),
                HasSubstr(".SH SEE ALSO")));
}

TEST_F(ManPageTest, GroupsOptionsAndExpressionsIntoSubsections) {
  const std::string plain = Plain(ManPage());
  EXPECT_THAT(
      plain,
      AllOf(HasSubstr(".SS Config"), HasSubstr(".SS Traversal"), HasSubstr(".SS Tests"), HasSubstr(".SS Operators")));
}

TEST_F(ManPageTest, DocumentsEveryGlobalAndPrimary) {
  const std::string plain = Plain(ManPage());
  for (const GlobalFlag& flag : Globals()) {
    EXPECT_THAT(plain, HasSubstr(flag.name)) << flag.name;
  }
  for (const registry::Descriptor& descriptor : registry::All()) {
    EXPECT_THAT(plain, HasSubstr(descriptor.name)) << descriptor.name;
  }
}

TEST_F(ManPageTest, MarksXffExtensions) {
  EXPECT_THAT(ManPage(), HasSubstr("(xff extension)"));
}

}  // namespace
}  // namespace xff::cli
