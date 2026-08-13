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

#include <string_view>

#include "absl/strings/ascii.h"

namespace xff::fuzzy {

bool Matches(std::string_view pattern, std::string_view text, bool fold_case) {
  // A greedy left-to-right scan is enough: for a pure subsequence test, taking the EARLIEST match
  // for each pattern character can never lose a match a later choice would have found, so there is
  // nothing to backtrack. (Ranking is what needs the alignment search, and there is no ranking
  // here - see TODO.md.)
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

}  // namespace xff::fuzzy
