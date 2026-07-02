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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace xff::format {

std::string Int(std::uint64_t value, char group_sep) {
  std::string digits = std::to_string(value);
  if (group_sep == '\0' || digits.size() <= 3) {
    return digits;
  }
  std::string out;
  out.reserve(digits.size() + (digits.size() - 1) / 3);
  for (std::size_t i = 0; i < digits.size(); ++i) {
    // A separator precedes digit `i` when the digits remaining (including it) are a
    // positive multiple of three -- grouping from the right without unsigned wrap.
    if (i != 0 && (digits.size() - i) % 3 == 0) {
      out.push_back(group_sep);
    }
    out.push_back(digits[i]);
  }
  return out;
}

std::string Size(std::uint64_t bytes, SizeUnits units) {
  const double base = units == SizeUnits::kSi ? 1000.0 : 1024.0;
  static constexpr std::array<std::string_view, 7> kIec{"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
  static constexpr std::array<std::string_view, 7> kSi{"B", "kB", "MB", "GB", "TB", "PB", "EB"};
  const std::array<std::string_view, 7>& suffix = units == SizeUnits::kSi ? kSi : kIec;
  if (static_cast<double>(bytes) < base) {
    return absl::StrCat(bytes, " ", suffix[0]);  // exact bytes below one unit
  }
  double value = static_cast<double>(bytes);
  std::size_t unit = 0;
  while (value >= base && unit + 1 < suffix.size()) {
    value /= base;
    ++unit;
  }
  return absl::StrFormat("%.1f %s", value, suffix[unit]);
}

std::string PadLeft(std::string_view text, std::size_t width) {
  if (text.size() >= width) {
    return std::string(text);
  }
  return std::string(width - text.size(), ' ') + std::string(text);
}

std::string PadRight(std::string_view text, std::size_t width) {
  std::string out(text);
  if (out.size() < width) {
    out.append(width - out.size(), ' ');
  }
  return out;
}

Table::Table(std::vector<Align> alignments) : aligns_(std::move(alignments)), widths_(aligns_.size(), 0) {}

void Table::AddRow(std::vector<std::string> cells) {
  for (std::size_t i = 0; i < cells.size() && i < widths_.size(); ++i) {
    widths_[i] = std::max(widths_[i], cells[i].size());
  }
  rows_.push_back(std::move(cells));
}

std::string Table::Render(std::string_view gap) const {
  std::string out;
  for (const std::vector<std::string>& row : rows_) {
    for (std::size_t col = 0; col < aligns_.size(); ++col) {
      if (col != 0) {
        out.append(gap);
      }
      const std::string_view cell = col < row.size() ? std::string_view(row[col]) : std::string_view();
      // The last column is not right-padded, so lines carry no trailing whitespace.
      if (aligns_[col] == Align::kRight) {
        out.append(PadLeft(cell, widths_[col]));
      } else if (col + 1 == aligns_.size()) {
        out.append(cell);
      } else {
        out.append(PadRight(cell, widths_[col]));
      }
    }
    out.push_back('\n');
  }
  return out;
}

}  // namespace xff::format
