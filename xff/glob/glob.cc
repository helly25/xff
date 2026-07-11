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

#include "xff/glob/glob.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace xff::glob {
namespace {

// Appends `c` to `re` as a literal, escaping the RE2 metacharacters. `*`, `?` and `[` are consumed
// by GlobToRegex before reaching here, so a bare one arriving as a literal (e.g. an unterminated
// class's `[`) is still escaped defensively.
void AppendLiteral(std::string& re, char c) {
  if (std::string_view(".+*?()|[]{}^$\\").find(c) != std::string_view::npos) {
    re += '\\';
  }
  re += c;
}

// Translates a glob `[...]` class starting at `pattern[i] == '['` into `re`, advancing `i` past the
// closing `]`. Handles glob negation (`[!` -> RE2 `[^`), the POSIX leading-`]`-is-a-literal rule
// (escaped `\]` for RE2), and POSIX bracket sub-expressions `[:class:]` / `[.collating.]` /
// `[=equivalence=]` (passed through verbatim, incl. their inner `]`, so RE2 gets `[[:alpha:]]` etc.).
void AppendCharClass(std::string& re, std::string_view pattern, std::size_t& i) {
  re += '[';
  ++i;
  if (i < pattern.size() && pattern[i] == '!') {  // glob negation `[!` -> RE2 `[^`
    re += '^';
    ++i;
  }
  if (i < pattern.size() && pattern[i] == ']') {  // a leading `]` is a literal member (POSIX)
    re += "\\]";
    ++i;
  }
  while (i < pattern.size() && pattern[i] != ']') {
    if (pattern[i] == '[' && i + 1 < pattern.size()
        && (pattern[i + 1] == ':' || pattern[i + 1] == '.' || pattern[i + 1] == '=')) {
      const char kind = pattern[i + 1];
      re += '[';
      re += kind;
      i += 2;
      while (i + 1 < pattern.size() && (pattern[i] != kind || pattern[i + 1] != ']')) {
        re += pattern[i];
        ++i;
      }
      if (i + 1 < pattern.size()) {  // the closing `:]` / `.]` / `=]`
        re += kind;
        re += ']';
        i += 2;
      }
      continue;
    }
    if (pattern[i] == '\\' && i + 1 < pattern.size()) {  // an escaped member (e.g. `\]`)
      re += '\\';
      re += pattern[i + 1];
      i += 2;
      continue;
    }
    re += pattern[i];
    ++i;
  }
  if (i < pattern.size()) {  // the closing `]`
    re += ']';
    ++i;
  }
}

}  // namespace

std::string GlobToRegex(std::string_view pattern) {
  std::string re;
  for (std::size_t i = 0; i < pattern.size();) {
    const char c = pattern[i];
    if (c == '*') {
      if (i + 1 < pattern.size() && pattern[i + 1] == '*') {
        std::size_t j = i;
        while (j < pattern.size() && pattern[j] == '*') {
          ++j;
        }
        const bool slash_before = i == 0 || pattern[i - 1] == '/';
        const bool slash_after = j == pattern.size() || pattern[j] == '/';
        if (slash_before && slash_after) {
          if (j == pattern.size()) {
            re += ".*";  // trailing `/**` (or a bare `**`): everything below
          } else {
            re += "(?:.*/)?";  // `**/`: zero or more leading directories
            ++j;               // also consume the separator that closed the segment
          }
        } else {
          re += "[^/]*";  // a `**` glued to other chars is just `*`
        }
        i = j;
        continue;
      }
      re += "[^/]*";
      ++i;
    } else if (c == '?') {
      re += "[^/]";
      ++i;
    } else if (c == '[') {
      AppendCharClass(re, pattern, i);
    } else if (c == '\\' && i + 1 < pattern.size()) {
      AppendLiteral(re, pattern[i + 1]);  // backslash escape: the next char is literal
      i += 2;
    } else {
      AppendLiteral(re, c);
      ++i;
    }
  }
  return re;
}

}  // namespace xff::glob
