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

#ifndef XFF_FUZZY_FUZZY_H_
#define XFF_FUZZY_FUZZY_H_

#include <string_view>

namespace xff::fuzzy {

// Approximate matching, fzf-style: `pattern` matches `text` when every character of the pattern
// appears in the text IN ORDER, with anything at all in between (a SUBSEQUENCE). `tmh` matches
// `the/main/header.h`, and that is the whole rule.
//
// Subsequence rather than bounded edit distance, which is the other reading of "fuzzy": edit
// distance answers "is this a typo of that" (a spell-checker's question), while a subsequence
// answers "can I type a few letters and find the file" (a file-finder's question, and the one fd /
// fzf / editors' quick-open answer). They are not interchangeable - `tmh` is nowhere near
// `the/main/header.h` by edit distance - and the interactive-finder reading is the one people
// coming to a find tool expect. The alternative is recorded in TODO.md rather than hidden here.
//
// An EMPTY pattern matches everything, exactly as an empty glob would: it constrains nothing.
[[nodiscard]] bool Matches(std::string_view pattern, std::string_view text, bool fold_case);

}  // namespace xff::fuzzy

#endif  // XFF_FUZZY_FUZZY_H_
