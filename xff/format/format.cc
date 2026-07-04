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
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace xff::format {
namespace {

// One aligned row: each cell padded to its column width per its Align, columns
// joined by `gap`, '\n'-terminated. The right-most cell is not right-padded, so no
// line carries trailing whitespace. Shared by Table and ColumnBuffer.
std::string RenderPaddedRow(
    const std::vector<std::string>& cells,
    const std::vector<Align>& aligns,
    const std::vector<std::size_t>& widths,
    std::string_view gap) {
  std::string out;
  for (std::size_t col = 0; col < aligns.size(); ++col) {
    if (col != 0) {
      out.append(gap);
    }
    const std::string_view cell = col < cells.size() ? std::string_view(cells[col]) : std::string_view();
    if (aligns[col] == Align::kRight) {
      out.append(PadLeft(cell, widths[col]));
    } else if (col + 1 == aligns.size()) {
      out.append(cell);
    } else {
      out.append(PadRight(cell, widths[col]));
    }
  }
  out.push_back('\n');
  return out;
}

// The scale chosen for `bytes`: `exact` (below one unit -> render the raw byte count) or
// `value` in `unit`. Shared by Size and SizeColumns so both pick the same unit.
struct Scaled {
  bool exact = false;
  double value = 0.0;
  std::string_view unit;
};

Scaled ScaleSize(std::uint64_t bytes, SizeUnits units) {
  const double base = units == SizeUnits::kSi ? 1000.0 : 1024.0;
  static constexpr std::array<std::string_view, 7> kIec{"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
  static constexpr std::array<std::string_view, 7> kSi{"B", "kB", "MB", "GB", "TB", "PB", "EB"};
  const std::array<std::string_view, 7>& suffix = units == SizeUnits::kSi ? kSi : kIec;
  if (static_cast<double>(bytes) < base) {
    return {.exact = true, .unit = suffix[0]};  // exact bytes below one unit
  }
  double value = static_cast<double>(bytes);
  std::size_t unit = 0;
  while (value >= base && unit + 1 < suffix.size()) {
    value /= base;
    ++unit;
  }
  return {.exact = false, .value = value, .unit = suffix[unit]};
}

}  // namespace

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
  const Scaled scaled = ScaleSize(bytes, units);
  if (scaled.exact) {
    return absl::StrCat(bytes, " ", scaled.unit);
  }
  return absl::StrFormat("%.1f %s", scaled.value, scaled.unit);
}

SizeParts SizeColumns(std::uint64_t bytes, SizeUnits units, unsigned fraction_digits) {
  const Scaled scaled = ScaleSize(bytes, units);
  SizeParts parts{.number = "", .suffix = std::string(scaled.unit)};
  if (scaled.exact) {
    // Exact bytes render as the integer; blank the fraction columns (a point plus
    // `fraction_digits` digits) so the (absent) decimal point still lines up under the
    // scaled rows when the number column is right-aligned.
    parts.number = absl::StrCat(bytes);
    if (fraction_digits > 0) {
      parts.number.append(fraction_digits + 1, ' ');
    }
  } else {
    parts.number = absl::StrFormat("%.*f", static_cast<int>(fraction_digits), scaled.value);
  }
  return parts;
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
    out.append(RenderPaddedRow(row, aligns_, widths_, gap));
  }
  return out;
}

ColumnBuffer::ColumnBuffer(std::vector<Align> aligns, std::vector<std::size_t> mins, std::size_t window)
    : aligns_(std::move(aligns)), widths_(std::move(mins)), window_(window), buffering_(window != 0) {
  widths_.resize(aligns_.size(), 0);  // one width per column, seeded from the mins
}

std::string ColumnBuffer::Add(std::vector<std::string> cells) {
  const auto widen = [this](const std::vector<std::string>& row) {
    for (std::size_t col = 0; col < row.size() && col < widths_.size(); ++col) {
      widths_[col] = std::max(widths_[col], row[col].size());
    }
  };
  if (buffering_) {
    widen(cells);
    buffer_.push_back(std::move(cells));
    // A bounded window that is now full flushes; kAll keeps buffering until Flush.
    if (window_ != kAll && buffer_.size() >= window_) {
      return Flush();
    }
    return "";
  }
  // Streaming after the window: the widths are frozen (decided by the buffered
  // window), so output is direct and columns never shift -- a rare cell wider than
  // anything in the window simply overflows its column. (window == 0 likewise streams
  // at the fixed minimum widths.)
  return RenderPaddedRow(cells, aligns_, widths_, "  ");
}

std::string ColumnBuffer::Flush() {
  if (!buffering_) {
    return "";
  }
  buffering_ = false;
  std::string out;
  for (const std::vector<std::string>& row : buffer_) {
    out.append(RenderPaddedRow(row, aligns_, widths_, "  "));
  }
  buffer_.clear();
  return out;
}

std::optional<std::size_t> ParseBufferWindow(std::string_view value) {
  if (value == "auto") {
    return std::size_t{100};
  }
  if (value == "off") {
    return std::size_t{0};
  }
  if (value == "all") {
    return ColumnBuffer::kAll;
  }
  if (value.empty()) {
    return std::nullopt;
  }
  // A trailing decimal SI multiplier scales a row count (case-insensitive). A byte-unit form
  // (a trailing 'B', as in 10MB / 10MiB) is a memory budget, not a row window, and leaves the
  // number un-parseable here so it falls through to nullopt.
  std::size_t multiplier = 1;
  switch (value.back()) {
    case 'k':
    case 'K': multiplier = 1'000; break;
    case 'm':
    case 'M': multiplier = 1'000'000; break;
    case 'g':
    case 'G': multiplier = 1'000'000'000; break;
    case 't':
    case 'T': multiplier = 1'000'000'000'000; break;
    default: break;
  }
  const std::string_view digits = multiplier == 1 ? value : value.substr(0, value.size() - 1);
  std::size_t number = 0;
  if (digits.empty() || !absl::SimpleAtoi(digits, &number)) {
    return std::nullopt;
  }
  return number * multiplier;
}

}  // namespace xff::format
