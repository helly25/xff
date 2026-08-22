// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
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

#ifndef XFF_SHARD_SHARD_H_
#define XFF_SHARD_SHARD_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"

// Forward-declared so the header need not pull in <re2/re2.h>. RE2 lives in
// namespace re2 (re2.h then adds a global `using re2::RE2`); the members hold
// compiled RE2 and the out-of-line destructor / move operations instantiate its
// deleter, so the definition is only needed in the .cc.
namespace re2 {
class RE2;
}  // namespace re2

// Recognizes sharded-file naming conventions: decodes a single filename into its
// logical shard identity (stem / index / total / tail). See docs/design.md
// "Sharded files" for the full model. This is the per-filename detection engine
// only - grouping a directory's matches into logical sets, dedup, completeness and
// the CLI surface (`--shards`) layer on top (#84 slices B onward).
namespace xff::shard {

// Which built-in naming convention a filename matched. This first slice carries the
// strong-in-name-signal schemes that a single filename decides on its own; the
// set-context schemes (split `xaa`, zip / rar volumes) join at grouping time.
enum class Scheme : std::uint8_t {
  kOf,          // <stem>-<index>-of-<total>[<tail>][<ext>] - TF/TFRecord, generic; 0-based
  kDotNum,      // <stem>.<NNN>                             - 7-Zip volumes, generic; 1-based
  kUnderscore,  // <stem>_<NNN>                             - numeric underscore suffix
  kCustom,      // a user `--shard-pattern=REGEX` (named captures stem/index/total?/dup?)
};

// The human-readable name of a scheme (matches the `--shards=SCHEME` spelling).
[[nodiscard]] std::string_view SchemeName(Scheme scheme);

// One shard filename decoded against a scheme. The set identity is (stem, total?,
// scheme) and the per-shard identity adds `index`; `tail` is deliberately excluded
// from both (redundant regenerations of one shard differ only in their tail).
struct Match {
  Scheme scheme = Scheme::kOf;
  std::string stem;                   // logical base name shared by the set
  std::int64_t index = 0;             // shard position, as written (not renormalized)
  std::optional<std::int64_t> total;  // declared count, when the scheme encodes one (kOf)
  std::string tail;                   // opaque generation id; EXCLUDED from identity
  std::string ext;                    // trailing extension after the number/tail; part of the set
  int width = 0;                      // digit count of the index field, for wildcard rendering
  std::string wildcard;               // the name with the index field replaced by `?` * width, tail
                                      // dropped (e.g. `f-???-of-003`); the set's canonical display name

  friend bool operator==(const Match&, const Match&) = default;
};

// How the optional opaque tail is recognized (see docs/design.md "the optional
// tail"). `pattern` is one regex with exactly one capturing group - that group is
// the `dup`; everything outside it is literal, so the separator lives in the regex.
// It is matched anchored at the string right after the shard number.
struct TailSpec {
  bool enabled = true;
  std::string pattern = R"(\.([0-9a-fA-F]{8,}))";
};

// Recognizes shard filenames. Compiles the schemes' patterns once, so a single
// instance is reused across a whole traversal. Move-only (it owns compiled RE2).
class Matcher {
 public:
  // A compiled custom `--shard-pattern` (defined in the .cc; the header only forward-declares it so
  // it need not pull in RE2). Held by unique_ptr so the incomplete type is fine here.
  struct CustomPattern;

  // Builds a matcher over all built-in schemes with the given tail spec, plus any `custom_patterns`
  // (RE2 with named groups: `stem` and `index` required, `total` and `dup` optional). Custom
  // patterns are tried before the built-ins, in order. Fails if the tail pattern is not a valid
  // one-group regex, or a custom pattern does not compile / lacks the required named groups.
  static absl::StatusOr<Matcher> Make(const TailSpec& tail = {}, absl::Span<const std::string> custom_patterns = {});

  Matcher(Matcher&&) noexcept;
  Matcher& operator=(Matcher&&) noexcept;
  Matcher(const Matcher&) = delete;
  Matcher& operator=(const Matcher&) = delete;
  ~Matcher();

  // Decodes one filename (a basename, not a path). nullopt if no scheme recognizes it.
  [[nodiscard]] std::optional<Match> Decode(std::string_view filename) const;

 private:
  Matcher(
      std::unique_ptr<re2::RE2> of_re,
      std::unique_ptr<re2::RE2> tail_re,
      std::vector<std::unique_ptr<CustomPattern>> custom);

  // Splits `rest` (everything after the shard number) into `tail` + `ext` on `out`.
  void ApplyTail(std::string_view rest, Match& out) const;

  // Tries the custom patterns in order; returns the first match, or nullopt.
  [[nodiscard]] std::optional<Match> DecodeCustom(std::string_view filename) const;

  std::unique_ptr<re2::RE2> of_re_;
  std::unique_ptr<re2::RE2> tail_re_;                   // null when the tail is disabled
  std::vector<std::unique_ptr<CustomPattern>> custom_;  // user --shard-pattern, tried before built-ins
};

}  // namespace xff::shard

#endif  // XFF_SHARD_SHARD_H_
