// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
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

#include "xff/cli/help_parse.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "xff/cli/help_model.h"

namespace xff::cli {

Inlines ParseInline(std::string_view text) {
  Inlines runs;
  std::string pending;  // literal text accumulated between code spans
  const auto flush_text = [&] {
    if (!pending.empty()) {
      runs.push_back(Inline{.style = Inline::Style::kText, .text = pending});
      pending.clear();
    }
  };
  std::size_t pos = 0;
  while (pos < text.size()) {
    if (text[pos] != '`') {
      pending.push_back(text[pos]);
      ++pos;
      continue;
    }
    if (pos + 1 < text.size() && text[pos + 1] == '`') {
      pending.push_back('`');  // a doubled backtick is one literal backtick
      pos += 2;
      continue;
    }
    // A single backtick opens a code span; it closes at the next backtick.
    const std::size_t close = text.find('`', pos + 1);
    if (close == std::string_view::npos) {
      pending.push_back('`');  // unterminated: the backtick is literal
      ++pos;
      continue;
    }
    flush_text();
    runs.push_back(Inline{.style = Inline::Style::kCode, .text = std::string(text.substr(pos + 1, close - pos - 1))});
    pos = close + 1;
  }
  flush_text();
  return runs;
}

Blocks ParseBlocks(std::string_view text) {
  Blocks blocks;
  std::vector<std::string_view> paragraph;  // whitespace-trimmed lines of the open paragraph
  const auto flush_paragraph = [&] {
    if (paragraph.empty()) {
      return;
    }
    blocks.push_back(Content{.node = Prose{.runs = ParseInline(absl::StrJoin(paragraph, " "))}});
    paragraph.clear();
  };

  const std::vector<std::string_view> lines = absl::StrSplit(text, '\n');
  std::size_t idx = 0;
  while (idx < lines.size()) {
    const std::string_view trimmed = absl::StripAsciiWhitespace(lines[idx]);
    if (absl::StartsWith(trimmed, "```")) {
      flush_paragraph();
      const std::string_view lang = absl::StripAsciiWhitespace(trimmed.substr(3));
      std::vector<std::string_view> body;  // verbatim lines, original indentation kept
      for (++idx; idx < lines.size() && absl::StripAsciiWhitespace(lines[idx]) != "```"; ++idx) {
        body.push_back(lines[idx]);
      }
      if (idx < lines.size()) {
        ++idx;  // consume the closing fence
      }
      blocks.push_back(Content{.node = Example{.text = absl::StrJoin(body, "\n"), .lang = std::string(lang)}});
    } else if (trimmed.empty()) {
      flush_paragraph();
      ++idx;
    } else {
      paragraph.push_back(trimmed);  // normalize authored wrapping; the backend reflows Prose
      ++idx;
    }
  }
  flush_paragraph();
  return blocks;
}

}  // namespace xff::cli
