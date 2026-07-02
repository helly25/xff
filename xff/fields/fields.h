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

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/time/time.h"
#include "xff/vfs/entry.h"

namespace xff::fields {

// The per-entry inputs a field renderer reads. Bundled into a context so new
// fields can draw on more inputs without changing every renderer's signature.
struct RenderContext {
  std::string_view path;                      // path as traversed
  std::string_view root;                      // command-line search root it was reached from (find %H); may be empty
  const vfs::Metadata& metadata;              // the entry's metadata
  int depth = 0;                              // 0 for a root operand, +1 per directory level
  absl::TimeZone tz = absl::LocalTimeZone();  // zone for {atime}/{mtime}/{ctime}/{btime} formatting; --timezone
  std::string_view time_format;               // default format for a time field with no {:qualifier}; --time-format
  const std::vector<std::string>* captures = nullptr;  // -regex groups for {0..N}: [0] whole match, 1..N groups
  const std::map<std::string, std::string>* defines = nullptr;  // --define values for {def.NAME}
  const std::map<std::string, std::string>* outputs = nullptr;  // -capture results for {capture.NAME}
  // Per-line match context for -grep=FORMAT: the 1-based number and text of the
  // matching line. `{line}` renders the number, `{text}` the line; both are empty
  // outside a -grep line (line_number unset), so they no-op in --template/-printf.
  std::optional<std::size_t> line_number;
  std::string_view line_text;
  // The matched span within the line (grep -o): `{match}` is the matched substring,
  // `{column}` its 1-based byte start. Empty/unset unless match_column is set (only
  // -grep=FORMAT computes it), so they no-op elsewhere.
  std::string_view match_text;
  std::optional<std::size_t> match_column;
};

namespace detail {
// A resolved field renderer: produces one field's value for an entry. `key` is
// the bound argument for dynamic/namespaced fields (a capture index, an
// {env.NAME} variable, ...), empty for builtins. Compile resolves each {field}
// to one of these once, so Render is a direct call per entry, not name matching.
using FieldFn = std::string (*)(std::string_view key, std::string_view qualifier, const RenderContext& context);
}  // namespace detail

// Renders {field} placeholder templates against a visited entry, substituting
// values from a RenderContext (path, root, metadata, depth). `{{` and `}}` emit
// literal braces; an unterminated or malformed `{` stays literal; an unknown
// field renders empty. This backs the --format/--template output and (gated)
// -exec substitution.
//
// Supported: {path} {root} {dir} {name}/{file} {stem} {ext}/{extension}
// {suffixes} {depth} {size} ({size:h} human-readable) {type} {inode} {links}
// {mode}/{perm} (octal) {user} {group}, and time fields {atime} {mtime} {ctime}
// {btime} with an optional qualifier {field:QUAL} -- a strftime format
// ({mtime:%Y-%m-%d}) or preset ({mtime:iso|epoch}); rendered in
// RenderContext::tz (the local zone unless --timezone overrides it), default
// ISO-8601. A qualifier may also be a "C-quoted string"
// ({mtime:"{\"t\":\"%H:%M\"}"}) so it can hold a literal '}' or ':'; inside it
// \" and \\ are escapes. A numeric placeholder {0}..{N} renders a regex capture
// from RenderContext::captures ({0} the whole match, {1}..{N} the groups; empty
// when unset or out of range) -- used by gated -exec after a -regex match. The
// {env.NAME} namespace renders a process environment variable, {def.NAME} a
// --define value, and {capture.NAME} a -capture result (each empty when unset).
// As a qualifier, a sed-style rewrite {field:s/PAT/REPL/flags} (any delimiter;
// flags g=all, i=ignore-case) post-processes the field's value via RE2.
//
// Compile parses the template once into literal/field segments; the resulting
// Template renders against many entries without re-scanning -- the hot path for
// --template (and -exec), which render every match.
class Template {
 public:
  static Template Compile(std::string_view tmpl);

  std::string Render(const RenderContext& context) const;

 private:
  // A literal run (fn == nullptr -> emit `literal`) or a field reference: fn is
  // the renderer and `key` its bound argument (capture index, {env.NAME} var, ...).
  struct Segment {
    std::string literal;
    detail::FieldFn fn = nullptr;
    std::string key;
    std::string qualifier;
    bool rewrite = false;  // qualifier is a sed-style s/PAT/REPL/ rewrite, applied post-render
  };

  std::vector<Segment> segments_;
};

// Convenience wrapper: Compile(tmpl).Render({path, metadata, depth}) with an
// empty root. Prefer Compile once + Render per entry on hot paths.
std::string Render(std::string_view tmpl, std::string_view path, const vfs::Metadata& metadata, int depth);

// Convenience wrapper rendering against a full context (so {root} resolves).
// Like the overload above, compiles per call -- prefer Compile once on hot paths.
std::string Render(std::string_view tmpl, const RenderContext& context);

}  // namespace xff::fields

#endif  // XFF_FIELDS_FIELDS_H_
