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

}  // namespace

std::optional<int> Score(std::string_view pattern, std::string_view text, bool fold_case) {
  if (pattern.empty()) {
    return 0;  // matches, and says nothing about how well - see Matches
  }
  if (pattern.size() > text.size()) {
    return std::nullopt;
  }
  // best[j] is the best score for the pattern prefix ending with its last character AT text[j];
  // `previous` is the same for the prefix one character shorter. Two rows are enough because a
  // pattern character can only follow the one before it.
  std::vector<int> previous(text.size(), kUnmatched);
  std::vector<int> best(text.size(), kUnmatched);
  for (std::string_view::size_type i = 0; i < pattern.size(); ++i) {
    // `reachable` carries the best score of any earlier alignment of the previous character, already
    // charged the gap penalty for the distance travelled. Updating it as j advances is what keeps
    // this O(pattern * text) instead of quadratic in the text.
    int reachable = kUnmatched;
    for (std::string_view::size_type j = 0; j < text.size(); ++j) {
      if (j > 0) {
        reachable = std::max(reachable - kGap, previous[j - 1]);
      }
      if (!SameChar(text[j], pattern[i], fold_case)) {
        best[j] = kUnmatched;
        continue;
      }
      const int here = kMatch + (IsWordStart(text, j) ? kWordStart : 0);
      if (i == 0) {
        // The first character may start anywhere, but the later it starts the worse it scores.
        best[j] = here - kGap * static_cast<int>(j);
        continue;
      }
      const int consecutive = j > 0 && previous[j - 1] != kUnmatched ? previous[j - 1] + kConsecutive : kUnmatched;
      const int from = std::max(reachable, consecutive);
      best[j] = from == kUnmatched ? kUnmatched : from + here;
    }
    previous.swap(best);
  }
  const auto found = absl::c_max_element(previous);
  if (found == previous.end() || *found == kUnmatched) {
    return std::nullopt;
  }
  return *found;
}

}  // namespace xff::fuzzy
