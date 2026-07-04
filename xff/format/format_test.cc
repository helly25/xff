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

#include <cstddef>
#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::format {
namespace {

using ::testing::Eq;
using ::testing::Optional;

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

TEST_F(FormatTest, SizeIecUsesBinaryUnits) {
  EXPECT_THAT(Size(0, SizeUnits::kIec), "0 B");
  EXPECT_THAT(Size(56, SizeUnits::kIec), "56 B");  // below one unit: exact bytes
  EXPECT_THAT(Size(1'023, SizeUnits::kIec), "1023 B");
  EXPECT_THAT(Size(1'024, SizeUnits::kIec), "1.0 KiB");
  EXPECT_THAT(Size(1'536, SizeUnits::kIec), "1.5 KiB");  // 1024 + 512
  EXPECT_THAT(Size(5'872'025, SizeUnits::kIec), "5.6 MiB");
  EXPECT_THAT(Size(1'024ULL * 1'024 * 1'024, SizeUnits::kIec), "1.0 GiB");
}

TEST_F(FormatTest, SizeSiUsesDecimalUnits) {
  EXPECT_THAT(Size(999, SizeUnits::kSi), "999 B");
  EXPECT_THAT(Size(1'000, SizeUnits::kSi), "1.0 kB");
  EXPECT_THAT(Size(5'872'025, SizeUnits::kSi), "5.9 MB");
  EXPECT_THAT(Size(2'000'000'000, SizeUnits::kSi), "2.0 GB");
}

TEST_F(FormatTest, SizeColumnsSplitsNumberAndSuffixWithFixedFraction) {
  // Scaled units get exactly `fraction_digits` decimals; the suffix comes back separately
  // for a left-aligned column.
  const SizeParts kib = SizeColumns(1'536, SizeUnits::kIec, 2);
  EXPECT_THAT(kib.number, "1.50");
  EXPECT_THAT(kib.suffix, "KiB");
  const SizeParts mb = SizeColumns(5'872'025, SizeUnits::kSi, 2);
  EXPECT_THAT(mb.number, "5.87");
  EXPECT_THAT(mb.suffix, "MB");
}

TEST_F(FormatTest, SizeColumnsBlanksTheFractionAreaForExactBytes) {
  // Exact bytes render as the integer; the fraction columns (point + digits) become
  // spaces so a right-aligned number column still lines the (absent) point up.
  const SizeParts b = SizeColumns(56, SizeUnits::kIec, 2);
  EXPECT_THAT(b.number, "56   ");  // "56" + three blanks standing in for ".DD"
  EXPECT_THAT(b.suffix, "B");
  // A one-digit byte and a one-digit scaled value share a number width, so right-aligning
  // the column lines the point (present or blanked) up.
  EXPECT_THAT(SizeColumns(5, SizeUnits::kIec, 2).number, "5   ");
  EXPECT_THAT(SizeColumns(1'024, SizeUnits::kIec, 2).number, "1.00");
}

TEST_F(FormatTest, SizeColumnsZeroPrecisionDropsThePoint) {
  EXPECT_THAT(SizeColumns(5'872'025, SizeUnits::kIec, 0).number, "6");  // rounded, no point
  EXPECT_THAT(SizeColumns(56, SizeUnits::kIec, 0).number, "56");        // no fraction area to blank
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

TEST_F(FormatTest, ColumnBufferAllAlignsAcrossEveryRow) {
  ColumnBuffer buf({Align::kLeft, Align::kRight}, {0, 0}, ColumnBuffer::kAll);
  EXPECT_THAT(buf.Add({"a", "1"}), "");     // buffered, nothing emitted yet
  EXPECT_THAT(buf.Add({"bb", "200"}), "");  // buffered
  // Flush aligns to the widest cells: col0 width 2, col1 width 3 (two-space gap).
  EXPECT_EQ(buf.Flush(), "a     1\nbb  200\n");
  EXPECT_THAT(buf.Flush(), "");  // idempotent
}

TEST_F(FormatTest, ColumnBufferWindowFlushesThenStreamsAndGrows) {
  ColumnBuffer buf({Align::kLeft, Align::kRight}, {0, 0}, /*window=*/2);
  EXPECT_THAT(buf.Add({"a", "1"}), "");                  // still buffering (1 < window)
  EXPECT_EQ(buf.Add({"bb", "20"}), "a    1\nbb  20\n");  // window full -> flush both at widths 2/2
  // Streaming now: a wider row grows the columns for itself (the flushed rows are out).
  EXPECT_EQ(buf.Add({"ccc", "300"}), "ccc  300\n");
  EXPECT_THAT(buf.Flush(), "");
}

TEST_F(FormatTest, ColumnBufferOffStreamsEachRowAtMinWidths) {
  ColumnBuffer buf({Align::kLeft, Align::kRight}, {3, 4}, /*window=*/0);
  // No buffering: each row emitted immediately, padded to the fixed minimum widths.
  EXPECT_EQ(buf.Add({"a", "1"}), "a       1\n");
  EXPECT_EQ(buf.Add({"bb", "200"}), "bb    200\n");
  EXPECT_THAT(buf.Flush(), "");
}

TEST_F(FormatTest, ParseBufferWindowKeywords) {
  EXPECT_THAT(ParseBufferWindow("auto"), Optional(Eq(std::size_t{100})));
  EXPECT_THAT(ParseBufferWindow("off"), Optional(Eq(std::size_t{0})));
  EXPECT_THAT(ParseBufferWindow("all"), Optional(Eq(ColumnBuffer::kAll)));
}

TEST_F(FormatTest, ParseBufferWindowBareInteger) {
  EXPECT_THAT(ParseBufferWindow("0"), Optional(Eq(std::size_t{0})));
  EXPECT_THAT(ParseBufferWindow("250"), Optional(Eq(std::size_t{250})));
}

TEST_F(FormatTest, ParseBufferWindowScalesTheDecimalSiMultiplier) {
  EXPECT_THAT(ParseBufferWindow("10k"), Optional(Eq(std::size_t{10'000})));
  EXPECT_THAT(ParseBufferWindow("10K"), Optional(Eq(std::size_t{10'000})));  // case-insensitive
  EXPECT_THAT(ParseBufferWindow("2M"), Optional(Eq(std::size_t{2'000'000})));
  EXPECT_THAT(ParseBufferWindow("1G"), Optional(Eq(std::size_t{1'000'000'000})));
}

TEST_F(FormatTest, ParseBufferWindowRejectsByteBudgetsAndGarbage) {
  // A byte-unit form (a trailing B) is a memory budget, not a row window -- handled elsewhere.
  EXPECT_THAT(ParseBufferWindow("10MB"), Eq(std::nullopt));
  EXPECT_THAT(ParseBufferWindow("10MiB"), Eq(std::nullopt));
  EXPECT_THAT(ParseBufferWindow("10B"), Eq(std::nullopt));
  EXPECT_THAT(ParseBufferWindow("garbage"), Eq(std::nullopt));
  EXPECT_THAT(ParseBufferWindow(""), Eq(std::nullopt));
  EXPECT_THAT(ParseBufferWindow("M"), Eq(std::nullopt));  // multiplier with no number
}

}  // namespace
}  // namespace xff::format
