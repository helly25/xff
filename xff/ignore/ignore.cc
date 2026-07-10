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

#include "xff/ignore/ignore.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "xff/glob/glob.h"
#include "xff/regex/regex.h"

namespace xff::ignore {
namespace {

// The glob -> RE2 translation lives in the shared //xff/glob lib (glob::GlobToRegex), used here and
// by the --regextype=GLOB matcher. The gitignore-specific stripping (leading `!` / anchoring `/` /
// trailing `/`) stays below; GlobToRegex receives the already-stripped body.

// Strips trailing unescaped spaces (gitignore ignores them unless backslash-quoted).
std::string_view RstripSpaces(std::string_view line) {
  std::size_t end = line.size();
  while (end > 0 && line[end - 1] == ' ' && !(end >= 2 && line[end - 2] == '\\')) {
    --end;
  }
  return line.substr(0, end);
}

}  // namespace

bool PatternList::Add(std::string_view pattern, bool negate) {
  std::string_view body = RstripSpaces(pattern);
  if (body.empty() || body.front() == '#') {
    return false;  // blank line or comment
  }
  bool negated = negate;
  if (body.front() == '!') {
    negated = !negated;
    body.remove_prefix(1);
  } else if (body.size() >= 2 && body.front() == '\\' && (body[1] == '#' || body[1] == '!')) {
    body.remove_prefix(1);  // `\#` / `\!`: a literal leading '#' or '!'
  }
  bool dir_only = false;
  if (!body.empty() && body.back() == '/') {
    dir_only = true;
    body.remove_suffix(1);
  }
  bool anchored = false;
  if (!body.empty() && body.front() == '/') {
    anchored = true;  // a leading '/' anchors to the base and is otherwise dropped
    body.remove_prefix(1);
  }
  if (body.find('/') != std::string_view::npos) {
    anchored = true;  // a separator anywhere else also anchors (gitignore rule)
  }
  if (body.empty()) {
    return false;  // nothing left to match (e.g. a lone "/")
  }
  // FullMatch anchors both ends, so an anchored pattern is the bare translation and
  // a floating one gets an optional leading-directory prefix ("match at any depth").
  const std::string translated = glob::GlobToRegex(body);
  const std::string re = anchored ? translated : absl::StrCat("(?:.*/)?", translated);
  absl::StatusOr<regex::Matcher> matcher = regex::Matcher::Compile(re, /*case_insensitive=*/false);
  if (!matcher.ok()) {
    return false;  // best-effort: skip a pattern that does not compile, as git does
  }
  patterns_.push_back(Pattern{.matcher = *std::move(matcher), .negated = negated, .dir_only = dir_only});
  return true;
}

void PatternList::AddPatterns(std::string_view text) {
  for (std::string_view line : absl::StrSplit(text, '\n')) {
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);  // tolerate CRLF ignore files
    }
    Add(line);
  }
}

PatternList PatternList::Parse(std::string_view text) {
  PatternList list;
  list.AddPatterns(text);
  return list;
}

Decision PatternList::Match(std::string_view relpath, bool is_dir) const {
  Decision result = Decision::kDefault;
  for (const Pattern& pattern : patterns_) {  // last matching pattern wins (gitignore)
    if (pattern.dir_only && !is_dir) {
      continue;
    }
    if (pattern.matcher.FullMatch(relpath)) {
      result = pattern.negated ? Decision::kInclude : Decision::kIgnore;
    }
  }
  return result;
}

}  // namespace xff::ignore
