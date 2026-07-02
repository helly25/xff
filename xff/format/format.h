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

#ifndef XFF_FORMAT_FORMAT_H_
#define XFF_FORMAT_FORMAT_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Shared number/text formatting for xff's human-facing output (the --summary
// table today; -ls column alignment and more later). The single place that knows
// how a count or a byte size is rendered, so every report groups digits and pads
// columns the same way. Size-unit rendering (--human) is layered on in a follow-up.
namespace xff::format {

// Renders `value` in decimal. When `group_sep` is non-zero the digits are grouped
// in threes from the right with that separator (e.g. Int(1234567, ',') ==
// "1,234,567"); the default (`\0`) leaves them ungrouped ("1234567").
std::string Int(std::uint64_t value, char group_sep = '\0');

// Right-justifies `text` in a field of `width` columns by prepending spaces (no
// truncation: a longer `text` is returned unchanged). For aligning a numeric
// column to its widest cell.
std::string PadLeft(std::string_view text, std::size_t width);

// Left-justifies `text` in a field of `width` columns by appending spaces (no
// truncation). For a left-aligned label column.
std::string PadRight(std::string_view text, std::size_t width);

// Per-column alignment for a Table cell.
enum class Align { kLeft, kRight };

// Accumulates rows of already-formatted cells and tracks the maximum width of each
// column as rows are added, so the whole table can be rendered aligned to its
// widest cell per column. This is the shared "column context" for xff's aligned
// reports (--summary now; -ls and other tabular output next) -- callers format each
// cell (via Int/human sizes/etc.), AddRow them, then Render once.
class Table {
 public:
  // One Align per column; every AddRow must supply exactly this many cells.
  explicit Table(std::vector<Align> alignments);

  // Appends a row of pre-formatted cells, widening each column to fit. Extra cells
  // beyond the column count are ignored; missing ones render empty.
  void AddRow(std::vector<std::string> cells);

  // Renders every row with each cell padded to its column's width per its Align,
  // columns separated by `gap`, each row '\n'-terminated. The right-most cell is not
  // right-padded, so no line carries trailing whitespace.
  std::string Render(std::string_view gap = "  ") const;

  std::size_t RowCount() const { return rows_.size(); }

 private:
  std::vector<Align> aligns_;
  std::vector<std::size_t> widths_;
  std::vector<std::vector<std::string>> rows_;
};

}  // namespace xff::format

#endif  // XFF_FORMAT_FORMAT_H_
