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

#ifndef XFF_RENDER_RENDER_H_
#define XFF_RENDER_RENDER_H_

#include <string>
#include <string_view>
#include <vector>

namespace xff::render {

// Output record format for matched paths. kPlain/kNul mirror find's
// -print/-print0; kJsonl is xff's modern one-object-per-line stream; kCsv/kTsv are
// streaming tabular (a one-time header via Header(), then one field row per match);
// kAligned/kMarkdown are buffered tabular (column widths need every row, so the whole
// table renders once via RenderTable): aligned space-padded columns, or a Markdown table.
enum class Format { kPlain, kNul, kJsonl, kCsv, kTsv, kAligned, kMarkdown };

// How path bytes are emitted (xff `--path-encoding`). kRaw writes the bytes
// verbatim (find-compatible default). kEscape C-escapes the backslash and control
// characters (`\n`, `\t`, `\r`, else `\xNN`) so a newline or control byte in a
// filename cannot corrupt the line-oriented kPlain stream. It applies only to
// kPlain: kNul stays raw by design (the NUL is the separator) and kJsonl always
// JSON-escapes regardless.
enum class PathEncoding { kRaw, kEscape };

// Formats matched paths into output records. Stateless aside from the format +
// encoding selectors; cheap to copy.
class Renderer {
 public:
  explicit Renderer(Format format, PathEncoding encoding = PathEncoding::kRaw) : format_(format), encoding_(encoding) {}

  // Returns the output record for `path`, terminator included:
  //   kPlain -> "path\n" (path C-escaped when encoding is kEscape),
  //   kNul   -> "path\0" (always raw), kJsonl -> {"path":"<JSON-escaped>"}\n,
  //   kCsv   -> a CSV field + "\n" (RFC-4180: quoted when it holds , " CR or LF),
  //   kTsv   -> a tab-escaped field + "\n" (\t \n \r \\ escaped).
  // `color` is an ANSI SGR parameter (e.g. "1;34"); when non-empty it wraps the
  // kPlain path in `\e[<color>m...\e[0m` (--color). Ignored for kNul (the NUL is the
  // separator) and the machine formats (kJsonl / kCsv / kTsv), which stay uncolored.
  std::string Record(std::string_view path, std::string_view color = {}) const;

  // The one-time header row for the tabular formats (kCsv / kTsv): the column names
  // terminated by a newline (currently the single default column `path`); empty for
  // kPlain / kNul / kJsonl. The driver emits it once before the records, unless
  // --no-header.
  std::string Header() const;

 private:
  Format format_;
  PathEncoding encoding_;
};

// Renders a whole buffered table -- a header row (`header`, the column names) then the data
// `rows` of already-rendered cells -- for the buffered tabular formats: kAligned (columns
// padded to their widest cell, space-separated, with a dashed underline under the header) or
// kMarkdown (a GitHub Markdown table: `| a | b |` rows, a `| --- | --- |` rule, cells with
// `|` and newlines escaped). `with_header` false (from --no-header) drops the header and its
// rule, emitting only the data rows. Buffered because a column's width needs every row, so
// the caller accumulates all matched rows first (O(matches) memory); returns "" for the
// streaming / non-tabular formats (which render per row via Record / EncodeTabularRow).
std::string RenderTable(
    Format format,
    const std::vector<std::string>& header,
    const std::vector<std::vector<std::string>>& rows,
    bool with_header = true);

// Encodes one row of already-rendered cell values for a tabular format (--columns): CSV
// (each cell RFC-4180 quoted, comma-joined) or TSV (each cell tab/newline/backslash
// escaped, tab-joined), plus a trailing newline. Shared by the header row (column names)
// and each match's row. Defined only for the streaming tabular formats (kCsv / kTsv); the
// buffered kAligned / kMarkdown render via RenderTable instead, and the non-tabular kPlain /
// kNul / kJsonl are not rows -- all of those yield an empty string.
std::string EncodeTabularRow(Format format, const std::vector<std::string>& cells);

}  // namespace xff::render

#endif  // XFF_RENDER_RENDER_H_
