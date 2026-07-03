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

#include "xff/color/color.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/vfs/entry.h"

namespace xff::color {
namespace {

using ::testing::Eq;
using ::testing::IsEmpty;

struct ColorTest : ::testing::Test {};

TEST_F(ColorTest, ResolveWhenLastOccurrenceWins) {
  EXPECT_THAT(ResolveWhen({}), Eq(When::kAuto));  // default
  EXPECT_THAT(ResolveWhen({"--color"}), Eq(When::kAlways));
  EXPECT_THAT(ResolveWhen({"--color=always"}), Eq(When::kAlways));
  EXPECT_THAT(ResolveWhen({"--color=auto"}), Eq(When::kAuto));
  EXPECT_THAT(ResolveWhen({"--color=never"}), Eq(When::kNever));
  EXPECT_THAT(ResolveWhen({"--color=always", "--color=never"}), Eq(When::kNever));
}

TEST_F(ColorTest, EnabledCombinesModeTtyAndNoColor) {
  EXPECT_TRUE(Enabled(When::kAlways, /*tty=*/false, /*no_color=*/true));  // explicit wins over NO_COLOR
  EXPECT_FALSE(Enabled(When::kNever, /*tty=*/true, /*no_color=*/false));
  EXPECT_TRUE(Enabled(When::kAuto, /*tty=*/true, /*no_color=*/false));
  EXPECT_FALSE(Enabled(When::kAuto, /*tty=*/false, /*no_color=*/false));  // not a terminal
  EXPECT_FALSE(Enabled(When::kAuto, /*tty=*/true, /*no_color=*/true));    // NO_COLOR set
}

TEST_F(ColorTest, CodeForTypeUsesLsLikeScheme) {
  EXPECT_THAT(CodeForType(vfs::FileType::kDirectory, 0755), Eq("1;34"));
  EXPECT_THAT(CodeForType(vfs::FileType::kSymlink, 0777), Eq("1;36"));
  EXPECT_THAT(CodeForType(vfs::FileType::kFifo, 0644), Eq("33"));
  EXPECT_THAT(CodeForType(vfs::FileType::kRegular, 0755), Eq("1;32"));  // executable regular file
  EXPECT_THAT(CodeForType(vfs::FileType::kRegular, 0644), IsEmpty());   // plain file: no color
}

}  // namespace
}  // namespace xff::color
