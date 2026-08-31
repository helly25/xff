// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
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
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
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
  return absl::c_all_of(text, [](char chr) { return chr >= '0' && chr <= '9'; });
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

// Builds a Match for a `<stem><sep><digits>` suffix scheme (kDotNum / kUnderscore);
// `sep` is the literal separator ('.' / '_'), reused to render the wildcard name.
[[nodiscard]] Match SuffixMatch(Scheme scheme, std::string_view stem, std::string_view digits, char sep) {
  const auto width = static_cast<int>(digits.size());
  return Match{
      .scheme = scheme,
      .stem = std::string(stem),
      .index = ToInt(digits),
      .width = width,
      .wildcard = absl::StrCat(stem, std::string_view(&sep, 1), std::string(width, '?')),
  };
}

}  // namespace

std::string_view SchemeName(Scheme scheme) {
  switch (scheme) {
    case Scheme::kOf: return "of";
    case Scheme::kDotNum: return "dotnum";
    case Scheme::kUnderscore: return "underscore";
    case Scheme::kCustom: return "custom";
  }
  return "unknown";
}

// A compiled custom `--shard-pattern`: the RE2 plus the 1-based capture indices of its named
// groups (`total_idx` / `dup_idx` are -1 when the pattern omits that optional group).
struct Matcher::CustomPattern {
  std::unique_ptr<re2::RE2> re;
  int stem_idx = 0;
  int index_idx = 0;
  int total_idx = -1;
  int dup_idx = -1;
};

Matcher::Matcher(
    std::unique_ptr<re2::RE2> of_re,
    std::unique_ptr<re2::RE2> tail_re,
    std::vector<std::unique_ptr<CustomPattern>> custom)
    : of_re_(std::move(of_re)), tail_re_(std::move(tail_re)), custom_(std::move(custom)) {}

Matcher::Matcher(Matcher&&) noexcept = default;
Matcher& Matcher::operator=(Matcher&&) noexcept = default;
Matcher::~Matcher() = default;

absl::StatusOr<Matcher> Matcher::Make(const TailSpec& tail, absl::Span<const std::string> custom_patterns) {
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
  std::vector<std::unique_ptr<CustomPattern>> custom;
  for (const std::string& pattern : custom_patterns) {
    auto re = std::make_unique<RE2>(pattern);
    if (!re->ok()) {
      return absl::InvalidArgumentError(absl::StrCat("invalid --shard-pattern regex '", pattern, "': ", re->error()));
    }
    // Named groups are the contract (RE2 uses `(?P<name>...)`); `stem` and `index` are required.
    const std::map<std::string, int>& groups = re->NamedCapturingGroups();
    const auto find = [&groups](std::string_view name) {
      const auto it = groups.find(std::string(name));
      return it == groups.end() ? -1 : it->second;
    };
    const int stem_idx = find("stem");
    const int index_idx = find("index");
    if (stem_idx < 0 || index_idx < 0) {
      return absl::InvalidArgumentError(
          absl::StrCat(
              "--shard-pattern '", pattern, "' must define the named groups (?P<stem>...) and (?P<index>...)"));
    }
    custom.push_back(
        std::make_unique<CustomPattern>(CustomPattern{
            .re = std::move(re),
            .stem_idx = stem_idx,
            .index_idx = index_idx,
            .total_idx = find("total"),
            .dup_idx = find("dup"),
        }));
  }
  return Matcher(std::move(of_re), std::move(tail_re), std::move(custom));
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

std::optional<Match> Matcher::DecodeCustom(std::string_view filename) const {
  for (const std::unique_ptr<CustomPattern>& pat : custom_) {
    const auto ngroups = static_cast<std::size_t>(pat->re->NumberOfCapturingGroups());
    std::vector<std::string_view> subs(ngroups + 1);
    if (!pat->re->Match(filename, 0, filename.size(), RE2::ANCHOR_BOTH, subs.data(), static_cast<int>(subs.size()))) {
      continue;
    }
    const std::string_view index_sv = subs[static_cast<std::size_t>(pat->index_idx)];
    if (index_sv.data() == nullptr) {
      continue;  // the required `index` group did not participate in this match
    }
    Match match{
        .scheme = Scheme::kCustom,
        .stem = std::string(subs[static_cast<std::size_t>(pat->stem_idx)]),
        .index = ToInt(index_sv),
        .width = static_cast<int>(index_sv.size()),
    };
    if (pat->total_idx >= 0) {
      if (const std::string_view total_sv = subs[static_cast<std::size_t>(pat->total_idx)]; !total_sv.empty()) {
        match.total = ToInt(total_sv);
      }
    }
    if (pat->dup_idx >= 0) {
      match.tail = std::string(subs[static_cast<std::size_t>(pat->dup_idx)]);  // dup excluded from identity
    }
    // Wildcard: mask the index group's span with `?` * width, in place. The group is a view into
    // `filename`, so its byte offset is known; everything else (including any dup) is kept verbatim.
    const auto off = static_cast<std::size_t>(index_sv.data() - filename.data());
    match.wildcard =
        absl::StrCat(filename.substr(0, off), std::string(match.width, '?'), filename.substr(off + index_sv.size()));
    return match;
  }
  return std::nullopt;
}

std::optional<Match> Matcher::Decode(std::string_view filename) const {
  // A user --shard-pattern wins over the built-ins (explicit intent), tried in order.
  if (std::optional<Match> custom = DecodeCustom(filename); custom.has_value()) {
    return custom;
  }
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
    // Rebuild with the index digits (subs[2]) replaced by `?` * width, keeping the verbatim total
    // (subs[3], padding preserved) and the extension, but dropping the opaque tail.
    match.wildcard = absl::StrCat(subs[1], "-", std::string(match.width, '?'), "-of-", subs[3], match.ext);
    return match;
  }
  // kDotNum: a trailing `.<digits>` (7-Zip volumes, generic).
  if (const auto split = TrailingDigits(filename, '.'); split.has_value()) {
    return SuffixMatch(Scheme::kDotNum, split->first, split->second, '.');
  }
  // kUnderscore: a trailing `_<digits>`.
  if (const auto split = TrailingDigits(filename, '_'); split.has_value()) {
    return SuffixMatch(Scheme::kUnderscore, split->first, split->second, '_');
  }
  return std::nullopt;
}

}  // namespace xff::shard
