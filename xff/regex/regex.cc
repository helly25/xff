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
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "re2/re2.h"
#include "xff/regex/backend.h"

namespace xff::regex {
namespace {

// The default grammar: RE2 (linear-time, no catastrophic backtracking). Holds the compiled RE2 and
// forwards each Matcher operation to it.
class Re2Backend final : public RegexBackend {
 public:
  explicit Re2Backend(std::unique_ptr<RE2> re) : re_(std::move(re)) {}

  bool FullMatch(std::string_view text) const override { return RE2::FullMatch(text, *re_); }

  bool PartialMatch(std::string_view text) const override { return RE2::PartialMatch(text, *re_); }

  std::optional<std::pair<std::size_t, std::size_t>> FindFirst(std::string_view text) const override {
    std::string_view match;  // submatch[0] = the whole match; its data() points into `text`
    if (!re_->Match(text, 0, text.size(), RE2::UNANCHORED, &match, 1)) {
      return std::nullopt;
    }
    return std::make_pair(static_cast<std::size_t>(match.data() - text.data()), match.size());
  }

  std::optional<std::vector<std::string>> FullMatchCaptures(std::string_view text) const override {
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

  std::string Rewrite(std::string_view text, std::string_view replacement, bool global) const override {
    std::string out(text);
    if (global) {
      RE2::GlobalReplace(&out, *re_, replacement);
    } else {
      RE2::Replace(&out, *re_, replacement);
    }
    return out;
  }

 private:
  std::unique_ptr<RE2> re_;
};

// The kExact grammar: a literal string match, no metacharacters. A core engine (always linked, no
// dependency), so --regextype=EXACT is always available. FullMatch is equality, PartialMatch a
// substring test, FindFirst the first occurrence, Rewrite a literal find/replace (no
// backreferences); `case_insensitive` folds ASCII case on both sides. There is no pattern to
// compile, so Compile(kExact) never fails.
class ExactBackend final : public RegexBackend {
 public:
  ExactBackend(std::string pattern, bool case_insensitive)
      : pattern_(std::move(pattern)),
        case_insensitive_(case_insensitive),
        needle_(case_insensitive_ ? absl::AsciiStrToLower(pattern_) : pattern_) {}

  bool FullMatch(std::string_view text) const override {
    return case_insensitive_ ? absl::EqualsIgnoreCase(text, pattern_) : text == pattern_;
  }

  bool PartialMatch(std::string_view text) const override { return FindFirst(text).has_value(); }

  std::optional<std::pair<std::size_t, std::size_t>> FindFirst(std::string_view text) const override {
    if (pattern_.empty()) {
      return std::make_pair(std::size_t{0}, std::size_t{0});  // empty needle matches at the start
    }
    // ASCII case-folding preserves byte positions, so the offset found in the lowered copy maps back
    // to `text` unchanged (the reported length is the pattern's).
    const std::size_t pos = case_insensitive_ ? absl::AsciiStrToLower(text).find(needle_) : text.find(needle_);
    if (pos == std::string::npos) {
      return std::nullopt;
    }
    return std::make_pair(pos, pattern_.size());
  }

  std::optional<std::vector<std::string>> FullMatchCaptures(std::string_view text) const override {
    if (!FullMatch(text)) {
      return std::nullopt;
    }
    return std::vector<std::string>{std::string(text)};  // index 0 = the whole match; no groups
  }

  std::string Rewrite(std::string_view text, std::string_view replacement, bool global) const override {
    if (pattern_.empty()) {
      return std::string(text);  // an empty needle rewrites nothing (avoids an infinite loop)
    }
    const std::string haystack = case_insensitive_ ? absl::AsciiStrToLower(text) : std::string(text);
    std::string out;
    std::size_t pos = 0;
    while (true) {
      const std::size_t hit = haystack.find(needle_, pos);
      if (hit == std::string::npos) {
        break;
      }
      out.append(text, pos, hit - pos);  // copy from the original text (preserves case)
      out.append(replacement);
      pos = hit + needle_.size();
      if (!global) {
        break;
      }
    }
    out.append(text.substr(pos));
    return out;
  }

 private:
  std::string pattern_;
  bool case_insensitive_;
  std::string needle_;  // == pattern_, ASCII-lowered when case_insensitive_
};

// The process-wide PCRE2 backend factory, empty when no PCRE2 backend is linked. Set once at
// static-init by the real backend's Pcre2Registrar (full build only); a Meyers static so the
// registrar in another TU can safely write it during static initialization.
Pcre2Factory& Pcre2FactorySlot() {
  static Pcre2Factory slot;
  return slot;
}

}  // namespace

void RegisterPcre2Backend(Pcre2Factory factory) {
  Pcre2FactorySlot() = std::move(factory);
}

bool Pcre2Available() {
  return static_cast<bool>(Pcre2FactorySlot());
}

absl::StatusOr<Matcher> Matcher::Compile(std::string_view pattern, bool case_insensitive, Grammar grammar) {
  switch (grammar) {
    case Grammar::kRe2: {
      RE2::Options options;
      options.set_case_sensitive(!case_insensitive);
      options.set_log_errors(false);  // surface failures via Status, not stderr
      auto re = std::make_unique<RE2>(pattern, options);
      if (!re->ok()) {
        return absl::InvalidArgumentError(absl::StrCat("invalid regular expression: ", re->error()));
      }
      return Matcher(std::make_unique<Re2Backend>(std::move(re)));
    }
    case Grammar::kExact:
      // A literal match: no pattern to compile, so this never fails.
      return Matcher(std::make_unique<ExactBackend>(std::string(pattern), case_insensitive));
    case Grammar::kPcre2: {
      // PCRE2 is a build-time extra: the real backend self-registers a factory (full build only).
      // When none is registered (lean build) the grammar is not available -- a distinct Unimplemented
      // state from an InvalidArgument bad pattern, and never a silent fallback to RE2.
      const Pcre2Factory& factory = Pcre2FactorySlot();
      if (!factory) {
        return absl::UnimplementedError("the PCRE2 regex grammar (--regextype=PCRE2) is not built into this binary");
      }
      absl::StatusOr<std::unique_ptr<const RegexBackend>> backend = factory(pattern, case_insensitive);
      if (!backend.ok()) {
        return backend.status();
      }
      return Matcher(*std::move(backend));
    }
  }
  return absl::InternalError("unknown regex grammar");  // unreachable: the enum is exhaustive
}

Matcher::Matcher(std::unique_ptr<const RegexBackend> backend) : backend_(std::move(backend)) {}

Matcher::~Matcher() = default;
Matcher::Matcher(Matcher&&) noexcept = default;
Matcher& Matcher::operator=(Matcher&&) noexcept = default;

bool Matcher::FullMatch(std::string_view text) const {
  return backend_->FullMatch(text);
}

bool Matcher::PartialMatch(std::string_view text) const {
  return backend_->PartialMatch(text);
}

std::optional<std::pair<std::size_t, std::size_t>> Matcher::FindFirst(std::string_view text) const {
  return backend_->FindFirst(text);
}

std::optional<std::vector<std::string>> Matcher::FullMatchCaptures(std::string_view text) const {
  return backend_->FullMatchCaptures(text);
}

std::string Matcher::Rewrite(std::string_view text, std::string_view replacement, bool global) const {
  return backend_->Rewrite(text, replacement, global);
}

}  // namespace xff::regex
