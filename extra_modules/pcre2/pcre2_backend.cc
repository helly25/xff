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

// pcre2.h REQUIRES this before the include: it selects the 8-bit code-unit API, and there is no
// constant form of it. NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "xff/license/notice.h"
#include "xff/regex/backend.h"

namespace {

// ReDoS guards: PCRE2 (unlike RE2) can backtrack, so cap the match compute and recursion depth so
// an adversarial pattern/subject cannot hang a walk. A no-match past the limit is reported as "no
// match" (the walk continues), never a crash.
constexpr std::uint32_t kMatchLimit = 1'000'000;
constexpr std::uint32_t kDepthLimit = 10'000;

// A non-null, NUL-terminated pointer for PCRE2, even for an empty view (whose data() may be null).
// PCRE2_SPTR is `const unsigned char*` while every caller holds `const char*`, so the cast is the
// whole job of this function - it exists precisely so the reinterpret_cast happens in ONE place.
PCRE2_SPTR Sptr(std::string_view text) {
  static constexpr std::array<char, 1> kEmpty = {'\0'};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): the char/uchar boundary, see above.
  return reinterpret_cast<PCRE2_SPTR>(text.empty() ? kEmpty.data() : text.data());
}

// Translates an RE2-style replacement (the Matcher::Rewrite contract: `\1`..`\9` backrefs, `\\` a
// literal backslash) into PCRE2 substitution syntax (`$1`, and `$$` for a literal `$`).
std::string Re2ReplacementToPcre2(std::string_view replacement) {
  std::string out;
  for (std::size_t i = 0; i < replacement.size(); ++i) {
    const char chr = replacement[i];
    if (chr == '\\' && i + 1 < replacement.size()) {
      const char next = replacement[i + 1];
      if (next >= '0' && next <= '9') {
        out += '$';  // \N -> $N
        out += next;
      } else {
        out += next;  // \\ -> \, and any other \x -> literal x
      }
      ++i;
    } else if (chr == '$') {
      out += "$$";  // a literal `$` must be escaped for PCRE2 substitution
    } else {
      out += chr;
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

  // Owns two raw PCRE2 handles it frees below, so copying would double-free and moving would need a
  // null-out dance nothing here wants: the factory hands each instance straight to a unique_ptr.
  Pcre2Backend(const Pcre2Backend&) = delete;
  Pcre2Backend& operator=(const Pcre2Backend&) = delete;
  Pcre2Backend(Pcre2Backend&&) = delete;
  Pcre2Backend& operator=(Pcre2Backend&&) = delete;

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
      // A span rather than the bare pointer PCRE2 hands back: the offsets are then indexed, which is
      // both checkable and what the style guide asks for instead of pointer arithmetic.
      const absl::Span<const PCRE2_SIZE> ovector = Ovector(data, 1);
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
      const absl::Span<const PCRE2_SIZE> ovector = Ovector(data, capture_count_ + 1);
      std::vector<std::string> captures;
      captures.reserve(capture_count_ + 1);
      for (std::size_t group = 0; group <= capture_count_; ++group) {  // [0] = whole match, [1..] = groups
        const PCRE2_SIZE start = ovector[2 * group];
        const PCRE2_SIZE end = ovector[(2 * group) + 1];
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
    // The output buffer is OURS, so it is declared in PCRE2's own element type and converted to a
    // std::string once at the end, which removes the char/uchar cast the std::string form needed.
    std::vector<PCRE2_UCHAR> out(text.size() + 16);  // initial guess; grown once on overflow
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
    // out_len is the result length (excluding the trailing NUL).
    return {out.begin(), out.begin() + static_cast<std::ptrdiff_t>(out_len)};
  }

 private:
  // PCRE2's ovector as a span of `pairs` start/end offsets, so callers index it instead of walking a
  // raw pointer. `pairs` is what the match data was created for, which is what bounds the array.
  static absl::Span<const PCRE2_SIZE> Ovector(pcre2_match_data* data, std::size_t pairs) {
    return absl::MakeConstSpan(pcre2_get_ovector_pointer(data), 2 * pairs);
  }

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
      std::vector<PCRE2_UCHAR>& out,
      PCRE2_SIZE* out_len) const {
    return pcre2_substitute(
        code_, Sptr(text), text.size(), 0, options, data, match_context_, Sptr(replacement), replacement.size(),
        out.data(), out_len);
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
    std::array<PCRE2_UCHAR, 256> buffer{};
    const int length = pcre2_get_error_message(error_code, buffer.data(), buffer.size());
    // Converted element-wise (uchar -> char is a value conversion the constructor performs), so no
    // cast; a negative length means PCRE2 could not render the message at all.
    const std::string message =
        length > 0 ? std::string(buffer.begin(), std::next(buffer.begin(), length)) : std::string("unknown error");
    return absl::InvalidArgumentError(absl::StrCat("invalid PCRE2 pattern at offset ", error_offset, ": ", message));
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
