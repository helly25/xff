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
#include <utility>
#include <vector>

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

// Advances `i` past the `[...]` class starting at `pattern[i] == '['` without emitting, mirroring
// AppendCharClass's traversal rules (leading `!` / `]`, POSIX `[:class:]` sub-expressions, escapes).
// Used by the brace scanner so a `,` or `}` sitting inside a class is not mistaken for a structural
// alternation separator or group close.
void SkipCharClass(std::string_view pattern, std::size_t& i) {
  ++i;
  if (i < pattern.size() && pattern[i] == '!') {
    ++i;
  }
  if (i < pattern.size() && pattern[i] == ']') {  // a leading `]` is a literal member, not the close
    ++i;
  }
  while (i < pattern.size() && pattern[i] != ']') {
    if (pattern[i] == '[' && i + 1 < pattern.size()
        && (pattern[i + 1] == ':' || pattern[i + 1] == '.' || pattern[i + 1] == '=')) {
      const char kind = pattern[i + 1];
      i += 2;
      while (i + 1 < pattern.size() && (pattern[i] != kind || pattern[i + 1] != ']')) {
        ++i;
      }
      if (i + 1 < pattern.size()) {
        i += 2;
      }
      continue;
    }
    if (pattern[i] == '\\' && i + 1 < pattern.size()) {
      i += 2;
      continue;
    }
    ++i;
  }
  if (i < pattern.size()) {
    ++i;
  }
}

// Translates a `*` run starting at `pattern[i] == '*'`, advancing `i` past it. A lone `*` is one
// segment's worth of non-slash (`[^/]*`); a `**` that is a WHOLE path segment crosses directories
// (gitignore semantics), otherwise a glued `**` degrades to a single `*`.
void AppendStar(std::string& re, std::string_view pattern, std::size_t& i) {
  if (i + 1 >= pattern.size() || pattern[i + 1] != '*') {
    re += "[^/]*";
    ++i;
    return;
  }
  std::size_t j = i;
  while (j < pattern.size() && pattern[j] == '*') {
    ++j;
  }
  const bool slash_before = i == 0 || pattern[i - 1] == '/';
  const bool slash_after = j == pattern.size() || pattern[j] == '/';
  if (!slash_before || !slash_after) {
    re += "[^/]*";  // a `**` glued to other chars is just `*`
  } else if (j == pattern.size()) {
    re += ".*";  // trailing `/**` (or a bare `**`): everything below
  } else {
    re += "(?:.*/)?";  // `**/`: zero or more leading directories
    ++j;               // also consume the separator that closed the segment
  }
  i = j;
}

// The shared glob -> RE2 translation. `braces` enables the SHGLOB brace-alternation extension (`{a,b}`
// -> `(?:a|b)`); with it off this is the plain path-glob translator (GLOB / gitignore).
void TranslateInto(std::string& re, std::string_view pattern, bool braces);

// Attempts to translate a brace alternation `{a,b,...}` starting at `pattern[i] == '{'`. On success
// appends `(?:...)` (each alternative recursively translated) and advances `i` past the closing `}`,
// returning true. Returns false with `i` unchanged when there is no matching top-level `}` or no
// top-level `,` - matching bash, a comma-less `{x}` (or an unbalanced `{`) is then a literal, so the
// caller emits the `{` as an ordinary character. `[...]` classes and `\`-escapes are skipped so their
// inner `,` / `}` / `{` are not treated as structure.
bool TryBraceGroup(std::string& re, std::string_view pattern, std::size_t& i) {
  std::vector<std::pair<std::size_t, std::size_t>> alts;  // [start, end) of each alternative
  std::size_t start = i + 1;
  std::size_t j = i + 1;
  int depth = 0;
  bool saw_comma = false;
  std::size_t end = std::string_view::npos;
  while (j < pattern.size()) {
    const char c = pattern[j];
    if (c == '\\' && j + 1 < pattern.size()) {
      j += 2;
    } else if (c == '[') {
      SkipCharClass(pattern, j);
    } else if (c == '{') {
      ++depth;
      ++j;
    } else if (c == '}' && depth > 0) {
      --depth;
      ++j;
    } else if (c == '}') {
      end = j;
      break;
    } else if (c == ',' && depth == 0) {
      alts.emplace_back(start, j);
      start = j + 1;
      saw_comma = true;
      ++j;
    } else {
      ++j;
    }
  }
  if (end == std::string_view::npos || !saw_comma) {
    return false;
  }
  alts.emplace_back(start, end);
  re += "(?:";
  for (std::size_t k = 0; k < alts.size(); ++k) {
    if (k != 0) {
      re += '|';
    }
    TranslateInto(re, pattern.substr(alts[k].first, alts[k].second - alts[k].first), true);
  }
  re += ')';
  i = end + 1;
  return true;
}

void TranslateInto(std::string& re, std::string_view pattern, bool braces) {
  for (std::size_t i = 0; i < pattern.size();) {
    const char c = pattern[i];
    if (c == '*') {
      AppendStar(re, pattern, i);
    } else if (c == '?') {
      re += "[^/]";
      ++i;
    } else if (c == '[') {
      AppendCharClass(re, pattern, i);
    } else if (c == '{' && braces && TryBraceGroup(re, pattern, i)) {
      // TryBraceGroup emitted the alternation and advanced `i`; a false return falls through to the
      // literal `{` below (a comma-less or unbalanced group).
    } else if (c == '\\' && i + 1 < pattern.size()) {
      AppendLiteral(re, pattern[i + 1]);  // backslash escape: the next char is literal
      i += 2;
    } else {
      AppendLiteral(re, c);
      ++i;
    }
  }
}

}  // namespace

std::string GlobToRegex(std::string_view pattern) {
  std::string re;
  TranslateInto(re, pattern, /*braces=*/false);
  return re;
}

std::string ShglobToRegex(std::string_view pattern) {
  std::string re;
  TranslateInto(re, pattern, /*braces=*/true);
  return re;
}

}  // namespace xff::glob
