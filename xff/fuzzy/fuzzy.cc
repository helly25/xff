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

#include "xff/fuzzy/fuzzy.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/ascii.h"

namespace xff::fuzzy {

bool Matches(std::string_view pattern, std::string_view text, bool fold_case) {
  // A greedy left-to-right scan is enough: for a pure subsequence test, taking the EARLIEST match
  // for each pattern character can never lose a match a later choice would have found, so there is
  // nothing to backtrack. Ranking is what needs the alignment search - that is Score, below.
  std::string_view::size_type at = 0;
  for (const char want : pattern) {
    for (;;) {
      if (at == text.size()) {
        return false;
      }
      const char have = text[at++];
      if (have == want || (fold_case && absl::ascii_tolower(have) == absl::ascii_tolower(want))) {
        break;
      }
    }
  }
  return true;
}

namespace {

// The weights. Deliberately small integers with a wide gap between "a character matched" and the
// bonuses, so a longer match always beats a shorter one and the bonuses only order matches of the
// same length. Tuned by the cases in fuzzy_test.cc, which are the real specification.
constexpr int kMatch = 16;          // per matched character
constexpr int kWordStart = 8;       // the character begins a word
constexpr int kConsecutive = 8;     // it follows the previous match immediately
constexpr int kGap = 1;             // per character skipped before a match
constexpr int kUnmatched = -1'000;  // "no alignment reaches here", never a real score

bool IsSeparator(char chr) {
  return chr == '_' || chr == '-' || chr == '.' || chr == '/' || chr == ' ';
}

// Whether text[at] begins a word: the start, anything after a separator, or a camelCase hump.
bool IsWordStart(std::string_view text, std::string_view::size_type at) {
  if (at == 0) {
    return true;
  }
  const char before = text[at - 1];
  return IsSeparator(before)
         || (absl::ascii_islower(static_cast<unsigned char>(before))
             && absl::ascii_isupper(static_cast<unsigned char>(text[at])));
}

bool SameChar(char have, char want, bool fold_case) {
  return have == want || (fold_case && absl::ascii_tolower(have) == absl::ascii_tolower(want));
}

// One row of the search: the best score for the pattern prefix whose LAST character is `want`, with
// that character placed at each position of `text`. `previous` is the same row for the prefix one
// character shorter (all kUnmatched when `want` is the first character), and the result lands in
// `best`. Split out of Score because the nesting, not the idea, is what made it hard to read.
void ScoreRow(
    std::string_view text,
    char want,
    bool fold_case,
    bool first,
    const std::vector<int>& previous,
    std::vector<int>& best) {
  // `reachable` carries the best score of any earlier alignment of the previous character, already
  // charged the gap penalty for the distance travelled. Updating it as the position advances is what
  // keeps this linear in the text rather than quadratic.
  int reachable = kUnmatched;
  for (std::string_view::size_type at = 0; at < text.size(); ++at) {
    if (at > 0) {
      reachable = std::max(reachable - kGap, previous[at - 1]);
    }
    if (!SameChar(text[at], want, fold_case)) {
      best[at] = kUnmatched;
      continue;
    }
    const int here = kMatch + (IsWordStart(text, at) ? kWordStart : 0);
    if (first) {
      // The first character may start anywhere, but the later it starts the worse it scores.
      best[at] = here - (kGap * static_cast<int>(at));
      continue;
    }
    const int consecutive = at > 0 && previous[at - 1] != kUnmatched ? previous[at - 1] + kConsecutive : kUnmatched;
    const int from = std::max(reachable, consecutive);
    best[at] = from == kUnmatched ? kUnmatched : from + here;
  }
}

}  // namespace

std::optional<int> Score(std::string_view pattern, std::string_view text, bool fold_case) {
  if (pattern.empty()) {
    return 0;  // matches, and says nothing about how well - see Matches
  }
  if (pattern.size() > text.size()) {
    return std::nullopt;
  }
  // Two rows are enough, because a pattern character can only follow the one before it.
  std::vector<int> previous(text.size(), kUnmatched);
  std::vector<int> best(text.size(), kUnmatched);
  for (std::string_view::size_type i = 0; i < pattern.size(); ++i) {
    ScoreRow(text, pattern[i], fold_case, /*first=*/i == 0, previous, best);
    previous.swap(best);
  }
  const auto found = absl::c_max_element(previous);
  if (found == previous.end() || *found == kUnmatched) {
    return std::nullopt;
  }
  return *found;
}

std::optional<int> Percent(std::string_view pattern, std::string_view text, bool fold_case) {
  const std::optional<int> score = Score(pattern, text, fold_case);
  if (!score.has_value()) {
    return std::nullopt;
  }
  if (pattern.empty()) {
    return 100;
  }
  const std::optional<int> ceiling = Score(pattern, pattern, fold_case);
  if (!ceiling.has_value() || *ceiling <= 0) {
    return std::nullopt;
  }
  const std::int64_t scaled = (static_cast<std::int64_t>(*score) * 100) / *ceiling;
  return static_cast<int>(std::clamp<std::int64_t>(scaled, 0, 100));
}

}  // namespace xff::fuzzy
