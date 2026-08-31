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

#ifndef XFF_MATCHING_MIME_MIME_H_
#define XFF_MATCHING_MIME_MIME_H_

#include <optional>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/types/span.h"

namespace xff::mime {

struct TypeInfo {
  std::string_view type;
  std::string_view description;
  std::string_view source;
  std::string_view charset;
  std::optional<bool> compressible;
  absl::Span<const std::string_view> aliases;
  absl::Span<const std::string_view> extensions;

  std::string_view Category() const;
};

enum class ConflictPolicy { kError, kFirst, kLast };

// Builds and publishes an immutable process-vocabulary snapshot from the curated core, every linked
// data layer, then the JSON files in command-line order. A later layer overrides an earlier one.
// Conflict policy applies to two different types claiming the same extension inside one input file.
// Published snapshots remain alive for the process lifetime, so returned views remain valid.
absl::Status Configure(absl::Span<const std::string> files, ConflictPolicy conflicts);

// The complete metadata for the file named `name`. Its strings and spans view an immutable,
// process-retained vocabulary snapshot and therefore remain valid for the process lifetime.
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
std::string_view TypeForName(std::string_view name);

// All canonical types in deterministic type-name order. The span and its records view an immutable,
// process-retained vocabulary snapshot; extension/name matching should use InfoForName instead.
absl::Span<const TypeInfo> Types();

}  // namespace xff::mime

#endif  // XFF_MATCHING_MIME_MIME_H_
