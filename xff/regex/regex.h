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

#ifndef XFF_REGEX_REGEX_H_
#define XFF_REGEX_REGEX_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "re2/re2.h"

namespace xff::regex {

// A compiled regular expression matched against whole strings: find's -regex
// matches the entire path, not a substring. Backed by RE2 (linear-time, no
// catastrophic backtracking). Move-only; safe to match concurrently once
// compiled. The -regextype grammar selection is layered on in a follow-up; this
// uses RE2's default (POSIX-extended-like) syntax.
class Matcher {
 public:
  // Compiles `pattern`; `case_insensitive` folds case (find's -iregex). Returns
  // an InvalidArgument error carrying RE2's diagnostic when it does not compile.
  static absl::StatusOr<Matcher> Compile(std::string_view pattern, bool case_insensitive);

  // True iff `text` matches the pattern in its entirety (both ends anchored).
  bool FullMatch(std::string_view text) const;

  // Like FullMatch, but on success returns the captured substrings: index 0 is
  // the whole match, 1..N the parenthesised groups (a group that did not take
  // part is empty). nullopt when `text` does not fully match. Backs the gated
  // {1}..{N} -exec placeholders.
  std::optional<std::vector<std::string>> FullMatchCaptures(std::string_view text) const;

  // Substitutes matches of the pattern within `text` with `replacement` (RE2
  // rewrite syntax: \1..\9 backrefs, \\ a literal backslash) -- the first match,
  // or every match when `global`. Returns the rewritten string. Backs the
  // {field:s/PAT/REPL/} rewrite qualifier.
  std::string Rewrite(std::string_view text, std::string_view replacement, bool global) const;

  Matcher(Matcher&&) = default;
  Matcher& operator=(Matcher&&) = default;

 private:
  explicit Matcher(std::unique_ptr<RE2> re) : re_(std::move(re)) {}

  std::unique_ptr<RE2> re_;
};

// Compiles Matchers on first use and caches them by (pattern, case_insensitive),
// so a -regex/-iregex pattern (or a -capture extraction regex) is compiled once
// per run rather than once per visited entry. Not thread-safe; the sequential walk
// owns one cache for the whole run.
class MatcherCache {
 public:
  // The compiled matcher for (pattern, case_insensitive), compiled and cached on
  // the first request and reused after. Returns nullptr if the pattern does not
  // compile (the failure is cached too, so it is not retried). The pointer stays
  // valid for the cache's lifetime (entries are never evicted).
  const Matcher* GetOrCompile(std::string_view pattern, bool case_insensitive);

 private:
  std::map<std::pair<std::string, bool>, std::optional<Matcher>> cache_;
};

}  // namespace xff::regex

#endif  // XFF_REGEX_REGEX_H_
