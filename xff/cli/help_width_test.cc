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

#include "xff/cli/help_width.h"

#include <optional>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

namespace xff::cli {
namespace {

using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::Eq;
using ::testing::HasSubstr;

struct ResolveHelpWidthTest : ::testing::Test {};

TEST_F(ResolveHelpWidthTest, AbsentFlagUsesTheDetectedTerminalWidth) {
  EXPECT_THAT(ResolveHelpWidth(std::nullopt, 120), IsOkAndHolds(Eq(120U)));
}

TEST_F(ResolveHelpWidthTest, AbsentFlagDoesNotWrapWhenTerminalUnknown) {
  EXPECT_THAT(ResolveHelpWidth(std::nullopt, 0), IsOkAndHolds(Eq(0U)));
}

TEST_F(ResolveHelpWidthTest, AutoMatchesTheAbsentBehaviorAndIsCaseInsensitive) {
  EXPECT_THAT(ResolveHelpWidth("auto", 100), IsOkAndHolds(Eq(100U)));
  EXPECT_THAT(ResolveHelpWidth("AUTO", 0), IsOkAndHolds(Eq(0U)));
}

TEST_F(ResolveHelpWidthTest, NoneAndZeroDisableWrapping) {
  EXPECT_THAT(ResolveHelpWidth("none", 120), IsOkAndHolds(Eq(0U)));
  EXPECT_THAT(ResolveHelpWidth("NONE", 120), IsOkAndHolds(Eq(0U)));
  EXPECT_THAT(ResolveHelpWidth("0", 120), IsOkAndHolds(Eq(0U)));
}

TEST_F(ResolveHelpWidthTest, APositiveIntegerIsAFixedWidthIgnoringTheTerminal) {
  EXPECT_THAT(ResolveHelpWidth("72", 120), IsOkAndHolds(Eq(72U)));
}

TEST_F(ResolveHelpWidthTest, ANonNumericValueIsAnError) {
  EXPECT_THAT(ResolveHelpWidth("wide", 0), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("--width")));
}

TEST_F(ResolveHelpWidthTest, ANegativeValueIsAnError) {
  EXPECT_THAT(ResolveHelpWidth("-5", 0), StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace xff::cli
