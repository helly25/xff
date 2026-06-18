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

#include <memory>
#include <string_view>
#include <utility>

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

bool Matcher::FullMatch(std::string_view text) const { return RE2::FullMatch(text, *re_); }

}  // namespace xff::regex
