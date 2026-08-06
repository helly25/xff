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

#include "xff/cli/wrap.h"

#include <cstddef>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace xff::cli {

std::string WrapText(
    std::string_view text,
    std::size_t width,
    std::string_view first_indent,
    std::string_view cont_indent) {
  std::string out;
  if (width == 0) {
    // No wrapping: the whole (already single-line) text on one indented line. Emitted
    // even when empty so it reproduces the pre-wrap "indent + text + newline" output.
    absl::StrAppend(&out, first_indent, text, "\n");
    return out;
  }
  std::string_view indent = first_indent;
  std::string line;
  const auto flush = [&] {
    absl::StrAppend(&out, indent, line, "\n");
    indent = cont_indent;
    line.clear();
  };
  for (const std::string_view word : absl::StrSplit(text, absl::ByAnyChar(" \t\n"), absl::SkipEmpty())) {
    // The content budget is the columns left once this line's indent is spent; a
    // fresh line always takes the word (even if it overflows) rather than looping.
    const std::size_t budget = width > indent.size() ? width - indent.size() : 0;
    if (line.empty()) {
      line = std::string(word);
    } else if (line.size() + 1 + word.size() <= budget) {
      absl::StrAppend(&line, " ", word);
    } else {
      flush();
      line = std::string(word);
    }
  }
  if (!line.empty()) {
    flush();
  }
  return out;
}

}  // namespace xff::cli
