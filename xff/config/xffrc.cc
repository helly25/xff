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

#include "xff/config/xffrc.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/str_split.h"

namespace xff::config {
namespace {

// Splits a selector token (known to end in ':') into base and config:
// "xff:debug:" -> {"xff", "debug"}; "common:" -> {"common", ""}; ":" -> {"", ""}.
RcLine SplitSelector(std::string_view selector) {
  selector.remove_suffix(1);  // drop the known trailing ':'
  const std::vector<std::string_view> parts = absl::StrSplit(selector, ':');
  RcLine line;
  if (!parts.empty()) {
    line.base = std::string(parts[0]);
  }
  if (parts.size() >= 2) {
    line.config = std::string(parts[1]);
  }
  return line;
}

}  // namespace

std::vector<RcLine> ParseXffrc(std::string_view text) {
  std::vector<RcLine> lines;
  for (const std::string_view raw : absl::StrSplit(text, '\n')) {
    const std::string_view trimmed = absl::StripAsciiWhitespace(raw);
    if (trimmed.empty() || trimmed.front() == '#') {
      continue;  // blank or comment
    }
    const std::vector<std::string_view> tokens = absl::StrSplit(trimmed, absl::ByAnyChar(" \t"), absl::SkipEmpty());
    RcLine line;
    std::size_t first_flag = 0;
    if (!tokens.empty() && tokens.front().back() == ':') {
      line = SplitSelector(tokens.front());  // a leading "selector:" token
      first_flag = 1;
    }
    for (std::size_t i = first_flag; i < tokens.size(); ++i) {
      line.flags.emplace_back(tokens[i]);
    }
    lines.push_back(std::move(line));
  }
  return lines;
}

}  // namespace xff::config
