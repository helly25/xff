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

#include <cstddef>
#include <string_view>
#include <vector>

#include "absl/functional/function_ref.h"

namespace xff::content {

std::vector<LineMatch> CollectLineMatches(
    std::string_view content,
    absl::FunctionRef<bool(std::string_view line)> matches) {
  std::vector<LineMatch> result;
  // A hand loop rather than absl::StrSplit: it yields grep's line semantics, where a
  // trailing '\n' does not create a phantom empty final line ("a\nb\n" is two lines)
  // and an empty file is zero lines, neither of which StrSplit expresses cleanly.
  std::size_t number = 0;
  std::size_t pos = 0;
  while (pos < content.size()) {
    const std::size_t newline = content.find('\n', pos);
    const std::size_t end = newline == std::string_view::npos ? content.size() : newline;
    std::string_view line = content.substr(pos, end - pos);
    ++number;
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);  // treat CRLF like LF so patterns match either
    }
    if (matches(line)) {
      result.push_back({.number = number, .text = line});
    }
    if (newline == std::string_view::npos) {
      break;
    }
    pos = newline + 1;
  }
  return result;
}

}  // namespace xff::content
