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

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"

namespace xff::regex {

// The grammar a Matcher compiles with - the engine behind `--regextype`. kRe2 is RE2 (linear-time,
// no catastrophic backtracking) - the default and find's historic behavior. kExact is a literal
// string match (no metacharacters), a core engine: FullMatch is equality, PartialMatch a substring
// test. kFnmatch is a flat shell wildcard (POSIX fnmatch: `*`/`?`/`[…]`, where `*` matches any
// character including `/`), a core engine - the `-name`/`-path` matching offered as a grammar.
// kPcre2 is PCRE2 (Perl syntax: backreferences, lookaround); it is a build extra, available only
// when its backend is linked, otherwise Compile returns an Unimplemented error (never a silent RE2
// fallback). kExact and kFnmatch need no real compilation, so Compile(...) never fails for them.
enum class Grammar { kRe2, kExact, kFnmatch, kPcre2 };

class RegexBackend;  // the concrete engine (backend.h); a Matcher owns one behind a unique_ptr

// A compiled regular expression. -regex matches the whole string (FullMatch); -rxc / -grep match
// anywhere (PartialMatch / FindFirst). The grammar (RE2 default, or PCRE2) is chosen at Compile and
// the engine held behind a RegexBackend, so this API is grammar-agnostic. Move-only; const after
// compile, so a compiled Matcher is safe to match concurrently.
class Matcher {
 public:
  // Compiles `pattern` under `grammar`; `case_insensitive` folds case (find's -iregex). Returns an
  // InvalidArgument error carrying the engine's diagnostic when the pattern does not compile, or an
  // Unimplemented error when `grammar`'s backend is not built into this binary.
  static absl::StatusOr<Matcher> Compile(
      std::string_view pattern,
      bool case_insensitive,
      Grammar grammar = Grammar::kRe2);

  // True iff `text` matches the pattern in its entirety (both ends anchored).
  bool FullMatch(std::string_view text) const;

  // True iff the pattern matches *anywhere* in `text` (unanchored). Backs the -rxc
  // regex content predicate ("the file's content matches"), the counterpart of
  // FullMatch's whole-string -regex.
  bool PartialMatch(std::string_view text) const;

  // The `[offset, length)` byte span of the leftmost unanchored match in `text`, or
  // nullopt when it does not match. Backs -grep's `{match}` (the matched substring)
  // and `{column}` (1-based match start); PartialMatch is the yes/no counterpart.
  std::optional<std::pair<std::size_t, std::size_t>> FindFirst(std::string_view text) const;

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

  ~Matcher();
  Matcher(Matcher&&) noexcept;
  Matcher& operator=(Matcher&&) noexcept;
  Matcher(const Matcher&) = delete;
  Matcher& operator=(const Matcher&) = delete;

 private:
  explicit Matcher(std::unique_ptr<const RegexBackend> backend);

  std::unique_ptr<const RegexBackend> backend_;
};

// Whether the PCRE2 grammar (kPcre2) is available in this binary, i.e. the real PCRE2 backend is
// linked and self-registered. False in the lean build (RE2 only), true in the full build. Drives
// the help "regex grammars" presence line and the Compile(kPcre2) availability check; -regextype=pcre
// on a binary where this is false is a clean error, never a silent RE2 fallback.
bool Pcre2Available();

}  // namespace xff::regex

#endif  // XFF_REGEX_REGEX_H_
