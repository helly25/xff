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

#include "xff/regex/regex.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "re2/re2.h"

namespace xff::regex {

absl::StatusOr<Matcher> Matcher::Compile(std::string_view pattern, bool case_insensitive) {
  RE2::Options options;
  options.set_case_sensitive(!case_insensitive);
  options.set_log_errors(false);  // surface failures via Status, not stderr
  auto re = std::make_unique<RE2>(pattern, options);
  if (!re->ok()) {
    return absl::InvalidArgumentError(absl::StrCat("invalid regular expression: ", re->error()));
  }
  return Matcher(std::move(re));
}

bool Matcher::FullMatch(std::string_view text) const {
  return RE2::FullMatch(text, *re_);
}

std::optional<std::vector<std::string>> Matcher::FullMatchCaptures(std::string_view text) const {
  const int groups = re_->NumberOfCapturingGroups();  // parenthesised groups, excluding the whole match
  const int nsubmatch = groups + 1;                   // index 0 holds the whole match
  std::vector<std::string_view> submatches(static_cast<std::size_t>(nsubmatch));
  if (!re_->Match(text, 0, text.size(), RE2::ANCHOR_BOTH, submatches.data(), nsubmatch)) {
    return std::nullopt;
  }
  std::vector<std::string> captures;
  captures.reserve(submatches.size());
  for (const std::string_view submatch : submatches) {  // [0] = whole match, [1..] = groups
    captures.emplace_back(submatch);
  }
  return captures;
}

std::string Matcher::Rewrite(std::string_view text, std::string_view replacement, bool global) const {
  std::string out(text);
  if (global) {
    RE2::GlobalReplace(&out, *re_, replacement);
  } else {
    RE2::Replace(&out, *re_, replacement);
  }
  return out;
}

}  // namespace xff::regex
