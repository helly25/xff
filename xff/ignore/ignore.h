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

#ifndef XFF_IGNORE_IGNORE_H_
#define XFF_IGNORE_IGNORE_H_

#include <cstddef>
#include <string_view>
#include <vector>

#include "xff/regex/regex.h"

namespace xff::ignore {

// The outcome of testing a path against a PatternList. kDefault means no pattern
// matched (the caller's own default applies); kIgnore means a plain pattern matched
// (exclude it); kInclude means a negation (`!pattern`, or a `--include` glob)
// matched, re-including a path an earlier pattern would exclude. Following
// gitignore, the LAST matching pattern in the list decides, so kInclude can
// override an earlier kIgnore and vice versa.
enum class Decision { kDefault, kIgnore, kInclude };

// One compiled gitignore-syntax pattern. Exposed only because PatternList stores
// these by value; construct a PatternList via Parse/Add rather than building these
// directly. The matcher tests a path RELATIVE to the pattern's base directory,
// '/'-separated with no leading '/'.
struct Pattern {
  regex::Matcher matcher;  // whole-string (FullMatch) match of the translated glob
  bool negated = false;    // leading '!': re-includes (Decision::kInclude)
  bool dir_only = false;   // trailing '/': matches directories only
};

// An ordered set of gitignore-syntax patterns: one ignore file's contents, or a
// set of command-line `--exclude` / `--include` globs. Patterns follow the
// gitignore spec -- `#` comments, blank-line skips, `!` negation, a trailing `/`
// for directory-only, a leading or embedded `/` anchoring to the base (else the
// pattern matches at any depth), `*` / `?` / `[...]`, and `**` (leading `**/` = any
// directory, trailing `/**` = everything below, `/**/` = zero or more directories).
// Move-only (the compiled regexes are); build once, then match. Matching is
// const and thread-safe.
//
// The matcher tests a single path; a caller that wants gitignore's "an ignored
// directory hides everything under it" must PRUNE a directory whose Match is
// kIgnore rather than descend and re-test each child.
class PatternList {
 public:
  PatternList() = default;

  // Builds a list from an ignore file's `text` (one pattern per line, `\n` or
  // `\r\n`); blank lines and `#` comments are skipped, and a pattern that does not
  // compile is skipped (best-effort, as git does).
  static PatternList Parse(std::string_view text);

  // Appends every pattern in `text` (one per line, like Parse) to this list, so
  // several ignore files in the same directory (e.g. .ignore then .xffignore) can
  // accumulate into one list with later files taking precedence (last match wins).
  void AddPatterns(std::string_view text);

  // Adds a single pattern. `negate` starts it as a re-include (for `--include`); a
  // leading `!` in `pattern` flips that again. Returns false (adding nothing) for a
  // blank line, a `#` comment, or a pattern that does not compile.
  bool Add(std::string_view pattern, bool negate = false);

  // The decision for `relpath` (relative to the base dir, '/'-separated, no leading
  // '/'); `is_dir` gates directory-only (trailing-'/') patterns. The last matching
  // pattern wins.
  Decision Match(std::string_view relpath, bool is_dir) const;

  bool empty() const { return patterns_.empty(); }

  std::size_t size() const { return patterns_.size(); }

  PatternList(PatternList&&) = default;
  PatternList& operator=(PatternList&&) = default;

 private:
  std::vector<Pattern> patterns_;
};

}  // namespace xff::ignore

#endif  // XFF_IGNORE_IGNORE_H_
