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

#include "absl/strings/match.h"

namespace xff::glob {
namespace {

// Appends `chr` to `re` as a literal, escaping the RE2 metacharacters. `*`, `?` and `[` are consumed
// by GlobToRegex before reaching here, so a bare one arriving as a literal (e.g. an unterminated
// class's `[`) is still escaped defensively.
void AppendLiteral(std::string& re, char chr) {
  if (absl::StrContains(std::string_view(".+*?()|[]{}^$\\"), chr)) {
    re += '\\';
  }
  re += chr;
}

// Translates a glob `[...]` class starting at `pattern[pos] == '['` into `re`, advancing `pos` past the
// closing `]`. Handles glob negation (`[!` -> RE2 `[^`), the POSIX leading-`]`-is-a-literal rule
// (escaped `\]` for RE2), and POSIX bracket sub-expressions `[:class:]` / `[.collating.]` /
// `[=equivalence=]` (passed through verbatim, incl. their inner `]`, so RE2 gets `[[:alpha:]]` etc.).
void AppendCharClass(std::string& re, std::string_view pattern, std::size_t& pos) {
  re += '[';
  ++pos;
  if (pos < pattern.size() && pattern[pos] == '!') {  // glob negation `[!` -> RE2 `[^`
    re += '^';
    ++pos;
  }
  if (pos < pattern.size() && pattern[pos] == ']') {  // a leading `]` is a literal member (POSIX)
    re += "\\]";
    ++pos;
  }
  while (pos < pattern.size() && pattern[pos] != ']') {
    if (pattern[pos] == '[' && pos + 1 < pattern.size()
        && (pattern[pos + 1] == ':' || pattern[pos + 1] == '.' || pattern[pos + 1] == '=')) {
      const char kind = pattern[pos + 1];
      re += '[';
      re += kind;
      pos += 2;
      while (pos + 1 < pattern.size() && (pattern[pos] != kind || pattern[pos + 1] != ']')) {
        re += pattern[pos];
        ++pos;
      }
      if (pos + 1 < pattern.size()) {  // the closing `:]` / `.]` / `=]`
        re += kind;
        re += ']';
        pos += 2;
      }
      continue;
    }
    if (pattern[pos] == '\\' && pos + 1 < pattern.size()) {  // an escaped member (e.g. `\]`)
      re += '\\';
      re += pattern[pos + 1];
      pos += 2;
      continue;
    }
    re += pattern[pos];
    ++pos;
  }
  if (pos < pattern.size()) {  // the closing `]`
    re += ']';
    ++pos;
  }
}

// Advances `pos` past the `[...]` class starting at `pattern[pos] == '['` without emitting, mirroring
// AppendCharClass's traversal rules (leading `!` / `]`, POSIX `[:class:]` sub-expressions, escapes).
// Used by the brace scanner so a `,` or `}` sitting inside a class is not mistaken for a structural
// alternation separator or group close.
void SkipCharClass(std::string_view pattern, std::size_t& pos) {
  ++pos;
  if (pos < pattern.size() && pattern[pos] == '!') {
    ++pos;
  }
  if (pos < pattern.size() && pattern[pos] == ']') {  // a leading `]` is a literal member, not the close
    ++pos;
  }
  while (pos < pattern.size() && pattern[pos] != ']') {
    if (pattern[pos] == '[' && pos + 1 < pattern.size()
        && (pattern[pos + 1] == ':' || pattern[pos + 1] == '.' || pattern[pos + 1] == '=')) {
      const char kind = pattern[pos + 1];
      pos += 2;
      while (pos + 1 < pattern.size() && (pattern[pos] != kind || pattern[pos + 1] != ']')) {
        ++pos;
      }
      if (pos + 1 < pattern.size()) {
        pos += 2;
      }
      continue;
    }
    if (pattern[pos] == '\\' && pos + 1 < pattern.size()) {
      pos += 2;
      continue;
    }
    ++pos;
  }
  if (pos < pattern.size()) {
    ++pos;
  }
}

// Translates a `*` run starting at `pattern[pos] == '*'`, advancing `pos` past it. A lone `*` is one
// segment's worth of non-slash (`[^/]*`); a `**` that is a WHOLE path segment crosses directories
// (gitignore semantics), otherwise a glued `**` degrades to a single `*`.
void AppendStar(std::string& re, std::string_view pattern, std::size_t& pos) {
  if (pos + 1 >= pattern.size() || pattern[pos + 1] != '*') {
    re += "[^/]*";
    ++pos;
    return;
  }
  std::size_t scan = pos;
  while (scan < pattern.size() && pattern[scan] == '*') {
    ++scan;
  }
  const bool slash_before = pos == 0 || pattern[pos - 1] == '/';
  const bool slash_after = scan == pattern.size() || pattern[scan] == '/';
  if (!slash_before || !slash_after) {
    re += "[^/]*";  // a `**` glued to other chars is just `*`
  } else if (scan == pattern.size()) {
    re += ".*";  // trailing `/**` (or a bare `**`): everything below
  } else {
    re += "(?:.*/)?";  // `**/`: zero or more leading directories
    ++scan;            // also consume the separator that closed the segment
  }
  pos = scan;
}

// The shared glob -> RE2 translation. `braces` enables the SHGLOB brace-alternation extension (`{a,b}`
// -> `(?:a|b)`); with it off this is the plain path-glob translator (GLOB / gitignore).
void TranslateInto(std::string& re, std::string_view pattern, bool braces);

// Attempts to translate a brace alternation `{a,b,...}` starting at `pattern[pos] == '{'`. On success
// appends `(?:...)` (each alternative recursively translated) and advances `pos` past the closing `}`,
// returning true. Returns false with `pos` unchanged when there is no matching top-level `}` or no
// top-level `,` - matching bash, a comma-less `{x}` (or an unbalanced `{`) is then a literal, so the
// caller emits the `{` as an ordinary character. `[...]` classes and `\`-escapes are skipped so their
// inner `,` / `}` / `{` are not treated as structure.
// Deliberate mutual recursion with TranslateInto to expand nested `{...}` groups; the depth is bounded
// by the pattern's brace nesting (a short, user-supplied string).
// NOLINTNEXTLINE(misc-no-recursion)
bool TryBraceGroup(std::string& re, std::string_view pattern, std::size_t& pos) {
  std::vector<std::pair<std::size_t, std::size_t>> alts;  // [start, end) of each alternative
  std::size_t start = pos + 1;
  std::size_t scan = pos + 1;
  int depth = 0;
  bool saw_comma = false;
  std::size_t end = std::string_view::npos;
  while (scan < pattern.size()) {
    const char chr = pattern[scan];
    if (chr == '\\' && scan + 1 < pattern.size()) {
      scan += 2;
    } else if (chr == '[') {
      SkipCharClass(pattern, scan);
    } else if (chr == '{') {
      ++depth;
      ++scan;
    } else if (chr == '}' && depth > 0) {
      --depth;
      ++scan;
    } else if (chr == '}') {
      end = scan;
      break;
    } else if (chr == ',' && depth == 0) {
      alts.emplace_back(start, scan);
      start = scan + 1;
      saw_comma = true;
      ++scan;
    } else {
      ++scan;
    }
  }
  if (end == std::string_view::npos || !saw_comma) {
    return false;
  }
  alts.emplace_back(start, end);
  re += "(?:";
  for (std::size_t idx = 0; idx < alts.size(); ++idx) {
    if (idx != 0) {
      re += '|';
    }
    TranslateInto(re, pattern.substr(alts[idx].first, alts[idx].second - alts[idx].first), true);
  }
  re += ')';
  pos = end + 1;
  return true;
}

// NOLINTNEXTLINE(misc-no-recursion): mutual recursion with TryBraceGroup for nested `{...}` (see above).
void TranslateInto(std::string& re, std::string_view pattern, bool braces) {
  for (std::size_t pos = 0; pos < pattern.size();) {
    const char chr = pattern[pos];
    if (chr == '*') {
      AppendStar(re, pattern, pos);
    } else if (chr == '?') {
      re += "[^/]";
      ++pos;
    } else if (chr == '[') {
      AppendCharClass(re, pattern, pos);
    } else if (chr == '{' && braces && TryBraceGroup(re, pattern, pos)) {
      // TryBraceGroup emitted the alternation and advanced `pos`; a false return falls through to the
      // literal `{` below (a comma-less or unbalanced group).
    } else if (chr == '\\' && pos + 1 < pattern.size()) {
      AppendLiteral(re, pattern[pos + 1]);  // backslash escape: the next char is literal
      pos += 2;
    } else {
      AppendLiteral(re, chr);
      ++pos;
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
