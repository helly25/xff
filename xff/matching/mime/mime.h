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

#ifndef XFF_MATCHING_MIME_MIME_H_
#define XFF_MATCHING_MIME_MIME_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/types/span.h"

namespace xff::mime {

struct TypeInfo {
  std::string type;
  std::string description;
  std::string source;
  std::string charset;
  std::optional<bool> compressible;
  std::vector<std::string> aliases;
  std::vector<std::string> extensions;

  std::string_view Category() const;
};

enum class ConflictPolicy { kError, kFirst, kLast };

// Builds the process vocabulary from the curated core, every linked data
// layer, then the JSON files in command-line order. A later layer overrides an
// earlier one. Conflict policy applies to two different types claiming the
// same extension inside one input file.
absl::Status Configure(absl::Span<const std::string> files, ConflictPolicy conflicts);

// The complete metadata for the file named `name`.
TypeInfo InfoForName(std::string_view name);

// The media (MIME) type for the file named `name`, derived from its extension.
// Returns application/octet-stream when the name has no recognised extension.
//
// Extension-based, not content-sniffed: no libmagic dependency (a deliberate
// no-heavyweight-native-dep choice). A content-sniffing backend can refine this
// later; the extension map is a fast, dependency-free first cut that backs the
// `-mime GLOB` predicate. Matching is case-insensitive on the extension (`.JPG` ==
// `.jpg`); a dotfile with no further dot (e.g. `.bashrc`) has no extension.
//
std::string TypeForName(std::string_view name);

// All canonical types in deterministic type-name order. Used by generated
// documentation; extension/name matching should use InfoForName instead.
std::vector<TypeInfo> Types();

}  // namespace xff::mime

#endif  // XFF_MATCHING_MIME_MIME_H_
