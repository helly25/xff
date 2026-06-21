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

#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

  Matcher(Matcher&&) = default;
  Matcher& operator=(Matcher&&) = default;

 private:
  explicit Matcher(std::unique_ptr<RE2> re) : re_(std::move(re)) {}

  std::unique_ptr<RE2> re_;
};

}  // namespace xff::regex

#endif  // XFF_REGEX_REGEX_H_
