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

#include "xff/shard/shard.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "re2/re2.h"

namespace xff::shard {
namespace {

// <stem>-<index>-of-<total><rest>: the stem is greedy, so the shard number binds to
// the last `-N-of-M` in the name; `rest` is everything after the total (tail + ext).
constexpr std::string_view kOfPattern = R"(^(.+)-(\d+)-of-(\d+)(.*)$)";

// True iff `text` is non-empty and every byte is an ASCII digit.
[[nodiscard]] bool AllDigits(std::string_view text) {
  if (text.empty()) {
    return false;
  }
  for (const char chr : text) {
    if (chr < '0' || chr > '9') {
      return false;
    }
  }
  return true;
}

// Parses an all-digit run to an integer, saturating benignly to 0 on overflow (a
// shard index with 19+ digits is not a real set); callers pass digit-only text.
[[nodiscard]] std::int64_t ToInt(std::string_view digits) {
  std::int64_t value = 0;
  if (!absl::SimpleAtoi(digits, &value)) {
    return 0;  // overflow on a pathologically long run; not a real shard index
  }
  return value;
}

// A trailing `<sep><digits>` suffix (`.001`, `_007`) yields (stem, digits); nullopt
// when the last `sep` is missing or is not followed by an all-digit run.
[[nodiscard]] std::optional<std::pair<std::string_view, std::string_view>> TrailingDigits(
    std::string_view name,
    char sep) {
  const std::size_t pos = name.rfind(sep);
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view digits = name.substr(pos + 1);
  if (!AllDigits(digits)) {
    return std::nullopt;
  }
  return std::make_pair(name.substr(0, pos), digits);
}

// Builds a Match for a `<stem><sep><digits>` suffix scheme (kDotNum / kUnderscore).
[[nodiscard]] Match SuffixMatch(Scheme scheme, std::string_view stem, std::string_view digits) {
  return Match{
      .scheme = scheme,
      .stem = std::string(stem),
      .index = ToInt(digits),
      .width = static_cast<int>(digits.size()),
  };
}

}  // namespace

std::string_view SchemeName(Scheme scheme) {
  switch (scheme) {
    case Scheme::kOf: return "of";
    case Scheme::kDotNum: return "dotnum";
    case Scheme::kUnderscore: return "underscore";
  }
  return "unknown";
}

Matcher::Matcher(std::unique_ptr<re2::RE2> of_re, std::unique_ptr<re2::RE2> tail_re)
    : of_re_(std::move(of_re)), tail_re_(std::move(tail_re)) {}

Matcher::Matcher(Matcher&&) noexcept = default;
Matcher& Matcher::operator=(Matcher&&) noexcept = default;
Matcher::~Matcher() = default;

absl::StatusOr<Matcher> Matcher::Make(const TailSpec& tail) {
  auto of_re = std::make_unique<RE2>(kOfPattern);
  if (!of_re->ok()) {
    return absl::InternalError(absl::StrCat("shard 'of' pattern failed to compile: ", of_re->error()));
  }
  std::unique_ptr<RE2> tail_re;
  if (tail.enabled && !tail.pattern.empty()) {
    tail_re = std::make_unique<RE2>(tail.pattern);
    if (!tail_re->ok()) {
      return absl::InvalidArgumentError(absl::StrCat("invalid --shard-tail regex: ", tail_re->error()));
    }
    if (tail_re->NumberOfCapturingGroups() != 1) {
      return absl::InvalidArgumentError(
          absl::StrCat(
              "--shard-tail regex must have exactly one capturing group (the dup), got ",
              tail_re->NumberOfCapturingGroups()));
    }
  }
  return Matcher(std::move(of_re), std::move(tail_re));
}

void Matcher::ApplyTail(std::string_view rest, Match& out) const {
  // Anchor at the start of `rest`: the tail (with its in-regex separator) sits
  // immediately after the shard number; anything past it is the extension. subs[0]
  // is the whole matched tail, subs[1] the sole capturing group (the opaque dup).
  std::array<std::string_view, 2> subs;
  if (tail_re_ != nullptr && !rest.empty()
      && tail_re_->Match(rest, 0, rest.size(), RE2::ANCHOR_START, subs.data(), subs.size())) {
    out.tail = std::string(subs[1]);
    out.ext = std::string(rest.substr(subs[0].size()));
    return;
  }
  out.ext = std::string(rest);
}

std::optional<Match> Matcher::Decode(std::string_view filename) const {
  // kOf: the self-describing `-of-` scheme, tried first (most specific).
  std::array<std::string_view, 5> subs;
  if (of_re_->Match(filename, 0, filename.size(), RE2::ANCHOR_BOTH, subs.data(), subs.size())) {
    Match match{
        .scheme = Scheme::kOf,
        .stem = std::string(subs[1]),
        .index = ToInt(subs[2]),
        .total = ToInt(subs[3]),
        .width = static_cast<int>(subs[2].size()),
    };
    ApplyTail(subs[4], match);
    return match;
  }
  // kDotNum: a trailing `.<digits>` (7-Zip volumes, generic).
  if (const auto split = TrailingDigits(filename, '.'); split.has_value()) {
    return SuffixMatch(Scheme::kDotNum, split->first, split->second);
  }
  // kUnderscore: a trailing `_<digits>`.
  if (const auto split = TrailingDigits(filename, '_'); split.has_value()) {
    return SuffixMatch(Scheme::kUnderscore, split->first, split->second);
  }
  return std::nullopt;
}

}  // namespace xff::shard
