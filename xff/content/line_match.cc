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

#include "xff/content/line_match.h"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

#include "absl/functional/function_ref.h"

namespace xff::content {
namespace {

// Grep line semantics, in one place: a hand loop rather than absl::StrSplit, so a trailing '\n'
// does not create a phantom empty final line ("a\nb\n" is two lines) and an empty file is zero
// lines, neither of which StrSplit expresses cleanly. Calls `fn(1-based number, line)` per line,
// with a trailing '\r' stripped (so CRLF matches like LF).
void ForEachLine(std::string_view content, absl::FunctionRef<void(std::size_t, std::string_view)> fn) {
  std::size_t number = 0;
  std::size_t pos = 0;
  while (pos < content.size()) {
    const std::size_t newline = content.find('\n', pos);
    const std::size_t end = newline == std::string_view::npos ? content.size() : newline;
    std::string_view line = content.substr(pos, end - pos);
    ++number;
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    fn(number, line);
    if (newline == std::string_view::npos) {
      break;
    }
    pos = newline + 1;
  }
}

}  // namespace

std::vector<LineMatch> CollectLineMatches(
    std::string_view content,
    absl::FunctionRef<bool(std::string_view line)> matches) {
  std::vector<LineMatch> result;
  ForEachLine(content, [&](std::size_t number, std::string_view line) {
    if (matches(line)) {
      result.push_back({.number = number, .text = line});
    }
  });
  return result;
}

std::vector<ContextLine> CollectLineMatchesWithContext(
    std::string_view content,
    absl::FunctionRef<bool(std::string_view line)> matches,
    std::size_t before,
    std::size_t after) {
  // Gather every line with its match flag, then select each match's [-before, +after] window.
  std::vector<ContextLine> all;
  ForEachLine(content, [&](std::size_t number, std::string_view line) {
    all.push_back({.number = number, .text = line, .is_match = matches(line), .group = 0});
  });
  const std::size_t count = all.size();
  std::vector<bool> emit(count, false);
  for (std::size_t i = 0; i < count; ++i) {
    if (!all[i].is_match) {
      continue;
    }
    const std::size_t lo = i > before ? i - before : 0;
    const std::size_t hi = after < count - i ? i + after : count - 1;  // count > 0 inside this loop
    for (std::size_t j = lo; j <= hi; ++j) {
      emit[j] = true;
    }
  }
  // Emit selected lines in order, opening a new group after each gap so a caller can separate
  // non-adjacent blocks. The first emitted line is group 0.
  std::vector<ContextLine> result;
  std::size_t group = 0;
  bool prev_emitted = false;
  bool first = true;
  for (std::size_t i = 0; i < count; ++i) {
    if (!emit[i]) {
      prev_emitted = false;
      continue;
    }
    if (!first && !prev_emitted) {
      ++group;
    }
    all[i].group = group;
    result.push_back(all[i]);
    prev_emitted = true;
    first = false;
  }
  return result;
}

}  // namespace xff::content
