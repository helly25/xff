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

#include "xff/format/format.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::format {
namespace {

struct FormatTest : ::testing::Test {};

TEST_F(FormatTest, IntUngroupedByDefault) {
  EXPECT_THAT(Int(0), "0");
  EXPECT_THAT(Int(1'234'567), "1234567");
}

TEST_F(FormatTest, IntGroupsInThreesFromTheRight) {
  EXPECT_THAT(Int(0, ','), "0");
  EXPECT_THAT(Int(12, ','), "12");
  EXPECT_THAT(Int(123, ','), "123");
  EXPECT_THAT(Int(1'234, ','), "1,234");
  EXPECT_THAT(Int(12'345, ','), "12,345");
  EXPECT_THAT(Int(123'456, ','), "123,456");
  EXPECT_THAT(Int(1'234'567, ','), "1,234,567");
  EXPECT_THAT(Int(1'000'000'000, ','), "1,000,000,000");
}

TEST_F(FormatTest, PadLeftRightJustifies) {
  EXPECT_THAT(PadLeft("42", 5), "   42");
  EXPECT_THAT(PadLeft("42", 2), "42");        // already wide enough
  EXPECT_THAT(PadLeft("12345", 3), "12345");  // never truncates
}

TEST_F(FormatTest, PadRightLeftJustifies) {
  EXPECT_THAT(PadRight("ab", 5), "ab   ");
  EXPECT_THAT(PadRight("ab", 2), "ab");
  EXPECT_THAT(PadRight("abcde", 3), "abcde");
}

TEST_F(FormatTest, TableAlignsColumnsToTheirWidestCell) {
  Table table({Align::kLeft, Align::kRight, Align::kRight});
  table.AddRow({"md", "1", "56"});
  table.AddRow({"txt", "2", "1,239"});
  table.AddRow({"total", "3", "1,295"});
  // Label left-padded to 5 ("total"); count right-aligned to 1; size right to 5.
  // Two-space gaps; the right-most (right-aligned) column leaves no trailing space.
  EXPECT_EQ(table.Render(), "md     1     56\ntxt    2  1,239\ntotal  3  1,295\n");
  EXPECT_THAT(table.RowCount(), 3U);
}

TEST_F(FormatTest, TableLeftColumnHasNoTrailingWhitespaceWhenLast) {
  Table table({Align::kRight, Align::kLeft});
  table.AddRow({"1", "a"});
  table.AddRow({"200", "bb"});
  // Right column is last + left-aligned, so it is emitted without trailing padding.
  EXPECT_EQ(table.Render(), "  1  a\n200  bb\n");
}

}  // namespace
}  // namespace xff::format
