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

#ifndef XFF_CONTENT_LINE_MATCH_H_
#define XFF_CONTENT_LINE_MATCH_H_

#include <cstddef>
#include <string_view>
#include <vector>

#include "absl/functional/function_ref.h"

namespace xff::content {

// One matching line within a file's content, as grep/ripgrep report it: the line's
// 1-based number and its text. Backs the `-grep` action's `{line}` / `{text}`
// fields.
struct LineMatch {
  std::size_t number = 0;   // 1-based line number
  std::string_view text{};  // the line's text, trailing '\r' stripped; aliases the searched content
};

// Splits `content` into lines and returns, in order, each line for which `matches`
// is true, paired with its 1-based number. Lines end at '\n' (a trailing '\r' is
// stripped so a CRLF file matches the same as LF); the byte after a final '\n' is
// not a line, so "a\nb\n" is two lines and "" is none, matching grep. The returned
// `text` views alias `content`, which must outlive the result.
std::vector<LineMatch> CollectLineMatches(
    std::string_view content,
    absl::FunctionRef<bool(std::string_view line)> matches);

// One line selected for grep-with-context output (grep -A/-B/-C): its 1-based number, text,
// whether it is a match (vs a surrounding context line), and its group index.
struct ContextLine {
  std::size_t number = 0;
  std::string_view text{};
  bool is_match = false;
  // 0-based group index; increments at each gap between emitted lines, so a caller prints a
  // group separator ("--") before every group after the first.
  std::size_t group = 0;
};

// Like CollectLineMatches, but also returns `before` lines preceding and `after` lines following
// each match (grep -B / -A; -C is before == after). Overlapping or adjacent windows merge, and a
// gap between emitted lines starts a new `group`. With before == after == 0 this returns exactly
// the match lines, each its own group (so a caller can suppress the separator when no context is
// set). Same line semantics as CollectLineMatches; the `text` views alias `content`.
std::vector<ContextLine> CollectLineMatchesWithContext(
    std::string_view content,
    absl::FunctionRef<bool(std::string_view line)> matches,
    std::size_t before,
    std::size_t after);

}  // namespace xff::content

#endif  // XFF_CONTENT_LINE_MATCH_H_
