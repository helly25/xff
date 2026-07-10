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

// The real PCRE2 regex backend: a composable build extra (--regextype=PCRE2, #85). This whole
// directory is removable - deleting it drops PCRE2 support entirely, and only //xff/cli:xff_full
// links it (via the //xff:xff_pcre select). It self-registers a factory with xff/regex (so
// xff::regex::Pcre2Available() flips true and Matcher::Compile(kPcre2) works) and its BSD-3 notice
// with xff/license, exactly the way the core engines register - the core never references PCRE2.

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "xff/license/license.h"
#include "xff/regex/backend.h"

namespace {

// ReDoS guards: PCRE2 (unlike RE2) can backtrack, so cap the match compute and recursion depth so
// an adversarial pattern/subject cannot hang a walk. A no-match past the limit is reported as "no
// match" (the walk continues), never a crash.
constexpr std::uint32_t kMatchLimit = 1'000'000;
constexpr std::uint32_t kDepthLimit = 10'000;

// A non-null, NUL-terminated pointer for PCRE2, even for an empty view (whose data() may be null).
PCRE2_SPTR Sptr(std::string_view text) {
  static constexpr char kEmpty[] = "";
  return reinterpret_cast<PCRE2_SPTR>(text.empty() ? kEmpty : text.data());
}

// Translates an RE2-style replacement (the Matcher::Rewrite contract: `\1`..`\9` backrefs, `\\` a
// literal backslash) into PCRE2 substitution syntax (`$1`, and `$$` for a literal `$`).
std::string Re2ReplacementToPcre2(std::string_view replacement) {
  std::string out;
  for (std::size_t i = 0; i < replacement.size(); ++i) {
    const char c = replacement[i];
    if (c == '\\' && i + 1 < replacement.size()) {
      const char next = replacement[i + 1];
      if (next >= '0' && next <= '9') {
        out += '$';  // \N -> $N
        out += next;
      } else {
        out += next;  // \\ -> \, and any other \x -> literal x
      }
      ++i;
    } else if (c == '$') {
      out += "$$";  // a literal `$` must be escaped for PCRE2 substitution
    } else {
      out += c;
    }
  }
  return out;
}

// A compiled PCRE2 pattern. The pcre2_code and match context are immutable after construction and
// safe to share across threads; each match allocates its own match_data (PCRE2's per-match state),
// so matching is thread-safe as the RegexBackend contract requires.
class Pcre2Backend final : public xff::regex::RegexBackend {
 public:
  Pcre2Backend(pcre2_code* code, pcre2_match_context* match_context, std::uint32_t capture_count)
      : code_(code), match_context_(match_context), capture_count_(capture_count) {}

  ~Pcre2Backend() override {
    pcre2_match_context_free(match_context_);
    pcre2_code_free(code_);
  }

  bool FullMatch(std::string_view text) const override {
    // Anchored at both ends: the pattern must match the entire subject (RE2::FullMatch semantics).
    return Matches(text, PCRE2_ANCHORED | PCRE2_ENDANCHORED);
  }

  bool PartialMatch(std::string_view text) const override { return Matches(text, 0); }

  std::optional<std::pair<std::size_t, std::size_t>> FindFirst(std::string_view text) const override {
    pcre2_match_data* data = pcre2_match_data_create(1, nullptr);  // one pair: the whole match
    const int rc = pcre2_match(code_, Sptr(text), text.size(), 0, 0, data, match_context_);
    std::optional<std::pair<std::size_t, std::size_t>> result;
    if (rc >= 0) {
      const PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(data);
      result = std::make_pair(static_cast<std::size_t>(ovector[0]), static_cast<std::size_t>(ovector[1] - ovector[0]));
    }
    pcre2_match_data_free(data);
    return result;
  }

  std::optional<std::vector<std::string>> FullMatchCaptures(std::string_view text) const override {
    pcre2_match_data* data = pcre2_match_data_create_from_pattern(code_, nullptr);
    const int rc =
        pcre2_match(code_, Sptr(text), text.size(), 0, PCRE2_ANCHORED | PCRE2_ENDANCHORED, data, match_context_);
    std::optional<std::vector<std::string>> result;
    if (rc >= 0) {
      const PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(data);
      std::vector<std::string> captures;
      captures.reserve(capture_count_ + 1);
      for (std::uint32_t group = 0; group <= capture_count_; ++group) {  // [0] = whole match, [1..] = groups
        const PCRE2_SIZE start = ovector[2 * group];
        const PCRE2_SIZE end = ovector[2 * group + 1];
        if (start == PCRE2_UNSET) {
          captures.emplace_back();  // a group that did not participate is empty (mirrors RE2)
        } else {
          captures.emplace_back(text.substr(start, end - start));
        }
      }
      result = std::move(captures);
    }
    pcre2_match_data_free(data);
    return result;
  }

  std::string Rewrite(std::string_view text, std::string_view replacement, bool global) const override {
    const std::string pcre2_replacement = Re2ReplacementToPcre2(replacement);
    const std::uint32_t options =
        PCRE2_SUBSTITUTE_OVERFLOW_LENGTH | (global ? PCRE2_SUBSTITUTE_GLOBAL : std::uint32_t{0});
    pcre2_match_data* data = pcre2_match_data_create_from_pattern(code_, nullptr);
    std::string out(text.size() + 16, '\0');  // initial guess; grown once on overflow
    PCRE2_SIZE out_len = out.size();
    int rc = Substitute(text, pcre2_replacement, options, data, out, &out_len);
    if (rc == PCRE2_ERROR_NOMEMORY) {
      out.resize(out_len);  // OVERFLOW_LENGTH set out_len to the required size (incl NUL)
      out_len = out.size();
      rc = Substitute(text, pcre2_replacement, options, data, out, &out_len);
    }
    pcre2_match_data_free(data);
    if (rc < 0) {
      return std::string(text);  // on any error, leave the text unchanged (defensive)
    }
    out.resize(out_len);  // out_len is the result length (excluding the trailing NUL)
    return out;
  }

 private:
  bool Matches(std::string_view text, std::uint32_t options) const {
    pcre2_match_data* data = pcre2_match_data_create(1, nullptr);
    const int rc = pcre2_match(code_, Sptr(text), text.size(), 0, options, data, match_context_);
    pcre2_match_data_free(data);
    return rc >= 0;
  }

  int Substitute(
      std::string_view text,
      const std::string& replacement,
      std::uint32_t options,
      pcre2_match_data* data,
      std::string& out,
      PCRE2_SIZE* out_len) const {
    return pcre2_substitute(
        code_, Sptr(text), text.size(), 0, options, data, match_context_, Sptr(replacement), replacement.size(),
        reinterpret_cast<PCRE2_UCHAR*>(out.data()), out_len);
  }

  pcre2_code* code_;
  pcre2_match_context* match_context_;
  std::uint32_t capture_count_;
};

// The factory registered with xff/regex: compiles `pattern` into a Pcre2Backend, or an
// InvalidArgument carrying PCRE2's diagnostic. Byte mode (no PCRE2_UTF) so arbitrary file bytes
// never trip UTF-8 validation; PCRE2_CASELESS folds case.
absl::StatusOr<std::unique_ptr<const xff::regex::RegexBackend>> CompilePcre2(
    std::string_view pattern,
    bool case_insensitive) {
  std::uint32_t options = 0;
  if (case_insensitive) {
    options |= PCRE2_CASELESS;
  }
  int error_code = 0;
  PCRE2_SIZE error_offset = 0;
  pcre2_code* code = pcre2_compile(Sptr(pattern), pattern.size(), options, &error_code, &error_offset, nullptr);
  if (code == nullptr) {
    PCRE2_UCHAR buffer[256];
    pcre2_get_error_message(error_code, buffer, sizeof(buffer) / sizeof(buffer[0]));
    return absl::InvalidArgumentError(
        absl::StrCat("invalid PCRE2 pattern at offset ", error_offset, ": ", reinterpret_cast<const char*>(buffer)));
  }
  pcre2_match_context* match_context = pcre2_match_context_create(nullptr);
  pcre2_set_match_limit(match_context, kMatchLimit);
  pcre2_set_depth_limit(match_context, kDepthLimit);
  std::uint32_t capture_count = 0;
  pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &capture_count);
  return std::make_unique<Pcre2Backend>(code, match_context, capture_count);
}

// Self-registration (alwayslink keeps this TU): the factory makes the PCRE2 grammar available, and
// the notice reproduces PCRE2's attribution in --help=notice for the full binary.
const xff::regex::Pcre2Registrar kRegisterPcre2Backend{&CompilePcre2};
const xff::license::Registrar kPcre2Notice{
    {.component = "PCRE2",
     .spdx = "BSD-3-Clause",
     .text = "Copyright (c) 1997-2024 University of Cambridge, Zoltan Herczeg. "
             "Redistribution permitted under the BSD-3-Clause license."}};

}  // namespace
