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

#ifndef XFF_FIELDS_FIELDS_H_
#define XFF_FIELDS_FIELDS_H_

#include <string>
#include <string_view>
#include <vector>

#include "xff/vfs/entry.h"

namespace xff::fields {

namespace detail {
// A resolved field renderer: produces one field's value for an entry. Compile
// resolves each {field} name to one of these once (constexpr dispatch table in
// fields.cc), so Render does a direct call per entry instead of name matching.
using FieldFn = std::string (*)(
    std::string_view qualifier, std::string_view path, const vfs::Metadata& metadata, int depth);
}  // namespace detail

// Renders {field} placeholder templates against a visited entry, substituting
// values derived from its `path`, `metadata`, and `depth`. `{{` and `}}` emit
// literal braces; an unterminated or malformed `{` stays literal; an unknown
// field renders empty. This backs the --format/--template output and (gated)
// -exec substitution.
//
// Supported: {path} {dir} {name}/{file} {stem} {ext}/{extension} {suffixes}
// {depth} {size} ({size:h} human-readable) {type} {inode} {links} {mode}/{perm}
// (octal) {user} {group}, and time fields {atime} {mtime} {ctime} {btime} with
// an optional qualifier {field:QUAL} -- a strftime format ({mtime:%Y-%m-%d}) or
// preset ({mtime:iso|epoch}); local time, default ISO-8601. A qualifier may be
// written as a "C-quoted string" ({mtime:"{\"t\":\"%H:%M\"}"}) so it can hold a
// literal '}' or ':' (\" and \\ are escapes). {root} layers on later.
//
// Compile parses the template once into literal/field segments; the resulting
// Template renders against many entries without re-scanning -- the hot path for
// --template (and -exec), which render every match.
class Template {
 public:
  static Template Compile(std::string_view tmpl);

  std::string Render(std::string_view path, const vfs::Metadata& metadata, int depth) const;

 private:
  // A literal run (fn == nullptr -> emit `literal`) or a field reference (-> fn).
  struct Segment {
    std::string literal;
    detail::FieldFn fn = nullptr;
    std::string qualifier;
  };

  std::vector<Segment> segments_;
};

// Convenience wrapper: Compile(tmpl).Render(...). Prefer Compile once + Render
// per entry on hot paths.
std::string Render(std::string_view tmpl, std::string_view path, const vfs::Metadata& metadata, int depth);

}  // namespace xff::fields

#endif  // XFF_FIELDS_FIELDS_H_
