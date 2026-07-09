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

#ifndef XFF_REGEX_BACKEND_H_
#define XFF_REGEX_BACKEND_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"

namespace xff::regex {

// The engine a `Matcher` delegates to for one compiled pattern: RE2 today, PCRE2 when that grammar
// is built in. `Matcher` owns a `RegexBackend` behind a `unique_ptr` and forwards each operation, so
// the public API is grammar-agnostic and the concrete engine (and its dependency) stays private.
//
// This is also the seam the future lean/full split gates on: a lean build can ship only the RE2
// backend and have the PCRE2 grammar resolve to a "not built in" error, while a full build links the
// real PCRE2 backend -- neither changes this interface, `Matcher`, or the `-regextype` selection.
// One backend instance per pattern; const after construction, so matching is thread-safe.
class RegexBackend {
 public:
  RegexBackend() = default;
  virtual ~RegexBackend() = default;

  RegexBackend(const RegexBackend&) = delete;
  RegexBackend& operator=(const RegexBackend&) = delete;
  RegexBackend(RegexBackend&&) = delete;
  RegexBackend& operator=(RegexBackend&&) = delete;

  // See the matching Matcher methods (regex.h) for the contract; the backend implements them.
  virtual bool FullMatch(std::string_view text) const = 0;
  virtual bool PartialMatch(std::string_view text) const = 0;
  virtual std::optional<std::pair<std::size_t, std::size_t>> FindFirst(std::string_view text) const = 0;
  virtual std::optional<std::vector<std::string>> FullMatchCaptures(std::string_view text) const = 0;
  virtual std::string Rewrite(std::string_view text, std::string_view replacement, bool global) const = 0;
};

// Compiles `pattern` into a PCRE2-backed RegexBackend (case-folding when `case_insensitive`), or an
// InvalidArgument error for a pattern PCRE2 rejects. The real PCRE2 backend -- built only into the
// full binary, from its own removable target under third_party/ -- provides one of these.
using Pcre2Factory =
    std::function<absl::StatusOr<std::unique_ptr<const RegexBackend>>(std::string_view pattern, bool case_insensitive)>;

// Registers the process-wide PCRE2 backend factory. Called once, at static-init, from the real
// backend's translation unit; linkage is presence -- a lean build links no such unit, so nothing
// registers and the PCRE2 grammar reports "not built in" (see Matcher::Compile / Pcre2Available).
// Static-init only; not thread-safe (the matter is resolved before the walk starts).
void RegisterPcre2Backend(Pcre2Factory factory);

// Self-registers `factory` on construction. Declare one at namespace scope in the real backend's TU
// (the target must be alwayslink so the registrar is not dropped):
//   const xff::regex::Pcre2Registrar kRegisterPcre2{&CompilePcre2};
struct Pcre2Registrar {
  explicit Pcre2Registrar(Pcre2Factory factory) { RegisterPcre2Backend(std::move(factory)); }
};

}  // namespace xff::regex

#endif  // XFF_REGEX_BACKEND_H_
