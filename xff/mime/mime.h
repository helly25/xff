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

#ifndef XFF_MIME_MIME_H_
#define XFF_MIME_MIME_H_

#include <string>
#include <string_view>

namespace xff::mime {

// The media (MIME) type for the file named `name`, derived from its extension via a
// curated static table of common types (image/*, audio/*, video/*, text/*, and
// common application/* and font/* types). Returns "application/octet-stream" -- the
// file(1) fallback -- when the name has no extension or an unrecognised one.
//
// Extension-based, not content-sniffed: no libmagic dependency (a deliberate
// no-heavyweight-native-dep choice). A content-sniffing backend can refine this
// later; the extension map is a fast, dependency-free first cut that backs the
// `-mime GLOB` predicate. Matching is case-insensitive on the extension (`.JPG` ==
// `.jpg`); a dotfile with no further dot (e.g. `.bashrc`) has no extension.
//
// NOTE (deferred richer data): callers reach the vocabulary only through this query, so the return
// can later become a `{key, data}` struct -- `key` the canonical lower-cased type (the -mime match
// target), `data` an extensible payload for file(1)-style details (description, category, ...).
// The table could then be a canonical, override-able vocabulary with a runtime-derived extension
// index. Not built until a metadata consumer or the table-override feature drives it; see TODO.md.
std::string_view TypeForName(std::string_view name);

}  // namespace xff::mime

#endif  // XFF_MIME_MIME_H_
