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

#include "xff/cli/globals.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::cli {
namespace {

using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::Le;
using ::testing::Ne;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::SizeIs;

struct GlobalsTest : ::testing::Test {};

TEST_F(GlobalsTest, EveryGlobalIsWellFormed) {
  EXPECT_THAT(Globals(), Not(IsEmpty()));
  for (const GlobalFlag& flag : Globals()) {
    EXPECT_THAT(flag.name, Not(IsEmpty())) << flag.name;
    EXPECT_THAT(flag.display, Not(IsEmpty())) << flag.name;
    EXPECT_THAT(flag.group, Not(IsEmpty())) << flag.name;
    ASSERT_THAT(flag.summary, Not(IsEmpty())) << flag.name;
    EXPECT_THAT(flag.summary, SizeIs(Le(90U))) << flag.name;
    EXPECT_THAT(flag.summary.back(), Ne('.')) << flag.name;
    EXPECT_THAT(flag.name.front(), Eq('-')) << flag.name;          // an option starts with a dash
    EXPECT_THAT(flag.display, HasSubstr(flag.name)) << flag.name;  // the header shows the canonical name
  }
}

TEST_F(GlobalsTest, LookupResolvesNameAndAlias) {
  EXPECT_THAT(LookupGlobal("--sort"), NotNull());
  EXPECT_THAT(LookupGlobal("--jobs"), Eq(LookupGlobal("-j")));        // alias resolves to the same entry
  EXPECT_THAT(LookupGlobal("--timezone"), Eq(LookupGlobal("--tz")));  // ditto
  EXPECT_THAT(LookupGlobal("--nonesuch"), IsNull());
}

TEST_F(GlobalsTest, EveryGlobalResolvesByItsOwnName) {
  for (const GlobalFlag& flag : Globals()) {
    EXPECT_THAT(LookupGlobal(flag.name), Eq(&flag)) << flag.name;
  }
}

}  // namespace
}  // namespace xff::cli
