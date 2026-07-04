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

#include "xff/render/render.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"

namespace xff::render {
namespace {

// Appends `path` to `out` as a JSON string body (without the surrounding
// quotes), escaping per RFC 8259: quote, backslash, and the control characters
// U+0000..U+001F (the common ones by name, the rest as \u00XX).
void AppendJsonEscaped(std::string_view path, std::string* out) {
  static constexpr std::string_view kHex = "0123456789abcdef";
  for (const char ch : path) {
    const auto byte = static_cast<unsigned char>(ch);
    switch (ch) {
      case '"': out->append("\\\""); break;
      case '\\': out->append("\\\\"); break;
      case '\n': out->append("\\n"); break;
      case '\r': out->append("\\r"); break;
      case '\t': out->append("\\t"); break;
      default:
        if (byte < 0x20) {
          out->append("\\u00");
          out->push_back(kHex[byte >> 4]);
          out->push_back(kHex[byte & 0x0F]);
        } else {
          out->push_back(ch);
        }
    }
  }
}

// Appends `path` to `out`, C-escaping the backslash and control characters so a
// newline or control byte in a filename cannot corrupt line-oriented output: `\\`,
// `\n`, `\t`, `\r`, and any other byte < 0x20 or 0x7F (DEL) as `\xNN`. Printable
// ASCII and high (UTF-8) bytes pass through verbatim.
void AppendCEscaped(std::string_view path, std::string* out) {
  static constexpr std::string_view kHex = "0123456789ABCDEF";
  for (const char ch : path) {
    const auto byte = static_cast<unsigned char>(ch);
    switch (ch) {
      case '\\': out->append("\\\\"); break;
      case '\n': out->append("\\n"); break;
      case '\t': out->append("\\t"); break;
      case '\r': out->append("\\r"); break;
      default:
        if (byte < 0x20 || byte == 0x7F) {
          out->append("\\x");
          out->push_back(kHex[byte >> 4]);
          out->push_back(kHex[byte & 0x0F]);
        } else {
          out->push_back(ch);
        }
    }
  }
}

// Appends `field` to `out` as one RFC-4180 CSV field: quoted only when it contains a
// comma, double-quote, CR, or LF, with each interior `"` doubled. A path never holds a
// NUL or '/'-embedded control that would break this, so no other escaping is needed.
void AppendCsvField(std::string_view field, std::string* out) {
  if (field.find_first_of(",\"\r\n") == std::string_view::npos) {
    out->append(field);
    return;
  }
  out->push_back('"');
  for (const char ch : field) {
    if (ch == '"') {
      out->push_back('"');  // double the interior quote
    }
    out->push_back(ch);
  }
  out->push_back('"');
}

// Appends `field` to `out` as one TSV field: a TSV field cannot hold a literal tab or
// newline, so escape tab / newline / CR / backslash as `\t` / `\n` / `\r` / `\\`.
void AppendTsvField(std::string_view field, std::string* out) {
  for (const char ch : field) {
    switch (ch) {
      case '\\': out->append("\\\\"); break;
      case '\t': out->append("\\t"); break;
      case '\n': out->append("\\n"); break;
      case '\r': out->append("\\r"); break;
      default: out->push_back(ch);
    }
  }
}

// One Markdown table cell: a cell is single-line and `|`-delimited, so escape `|` as `\|`,
// turn a newline into a space, and drop CR. Everything else passes through verbatim.
std::string MarkdownCell(std::string_view field) {
  std::string out;
  for (const char ch : field) {
    switch (ch) {
      case '|': out.append("\\|"); break;
      case '\n': out.push_back(' '); break;
      case '\r': break;
      default: out.push_back(ch);
    }
  }
  return out;
}

}  // namespace

std::string Renderer::Record(std::string_view path, std::string_view color) const {
  switch (format_) {
    case Format::kAligned:
    case Format::kMarkdown:
      // Buffered tabular: the whole table renders once via RenderTable, so a single record
      // has no standalone encoding here. Emit the raw path line as a defensive fallback (the
      // walk driver routes these formats through RenderTable, never Record).
      return absl::StrCat(path, "\n");
    case Format::kCsv: {
      std::string record;
      AppendCsvField(path, &record);
      record.push_back('\n');
      return record;
    }
    case Format::kJsonl: {
      std::string record = "{\"path\":\"";
      AppendJsonEscaped(path, &record);
      record.append("\"}\n");
      return record;
    }
    case Format::kNul: {
      std::string record(path);
      record.push_back('\0');
      return record;
    }
    case Format::kPlain: {
      std::string body;  // the path bytes (raw or C-escaped), before color + newline
      if (encoding_ == PathEncoding::kEscape) {
        AppendCEscaped(path, &body);
      } else {
        body = std::string(path);
      }
      if (color.empty()) {
        return absl::StrCat(body, "\n");
      }
      return absl::StrCat("\x1b[", color, "m", body, "\x1b[0m\n");
    }
    case Format::kTsv: {
      std::string record;
      AppendTsvField(path, &record);
      record.push_back('\n');
      return record;
    }
  }
  return absl::StrCat(path, "\n");  // unreachable: every Format returns above
}

std::string EncodeTabularRow(Format format, const std::vector<std::string>& cells) {
  switch (format) {
    case Format::kCsv:
    case Format::kTsv: {
      std::string out;
      bool first = true;
      for (const std::string& cell : cells) {
        if (!first) {
          out.push_back(format == Format::kTsv ? '\t' : ',');
        }
        first = false;
        if (format == Format::kTsv) {
          AppendTsvField(cell, &out);
        } else {
          AppendCsvField(cell, &out);
        }
      }
      out.push_back('\n');
      return out;
    }
    case Format::kAligned:
    case Format::kMarkdown:
    case Format::kJsonl:
    case Format::kNul:
    case Format::kPlain: return "";  // streaming per-row is csv/tsv only (buffered ones use RenderTable)
  }
  return "";  // unreachable: every Format handled above
}

std::string Renderer::Header() const {
  switch (format_) {
    case Format::kCsv: {
      std::string header;
      AppendCsvField("path", &header);  // slice 1: the single default column
      header.push_back('\n');
      return header;
    }
    case Format::kTsv: {
      std::string header;
      AppendTsvField("path", &header);
      header.push_back('\n');
      return header;
    }
    case Format::kAligned:
    case Format::kMarkdown:
    case Format::kJsonl:
    case Format::kNul:
    case Format::kPlain: return "";  // buffered formats emit their header inside RenderTable
  }
  return "";  // unreachable: every Format handled above
}

std::string RenderTable(
    Format format,
    const std::vector<std::string>& header,
    const std::vector<std::vector<std::string>>& rows,
    bool with_header) {
  const bool md = format == Format::kMarkdown;
  if (!md && format != Format::kAligned) {
    return "";  // streaming / non-tabular; rendered per row via Record / EncodeTabularRow
  }
  const std::size_t columns = header.size();
  if (columns == 0) {
    return "";
  }

  // Escape each cell for its format (md `|`-escapes and single-lines; aligned keeps bytes
  // verbatim, like -ls), then take each column's width as the widest shown cell: the header
  // (unless --no-header dropped it) plus every data row. md floors a column at 3 so the
  // `---` rule always fits. This holds every matched row in memory (O(matches)) because the
  // widths are only known once the walk ends; a --buffer-style bound is a planned follow-up.
  const auto encode = [md](std::string_view cell) { return md ? MarkdownCell(cell) : std::string(cell); };
  std::vector<std::string> head;
  head.reserve(columns);
  for (const std::string& name : header) {
    head.push_back(encode(name));
  }
  std::vector<std::vector<std::string>> data;
  data.reserve(rows.size());
  for (const std::vector<std::string>& row : rows) {
    std::vector<std::string> cells;
    cells.reserve(columns);
    for (std::size_t col = 0; col < columns; ++col) {
      cells.push_back(encode(col < row.size() ? std::string_view(row[col]) : std::string_view()));
    }
    data.push_back(std::move(cells));
  }
  std::vector<std::size_t> width(columns, md ? 3 : 0);
  if (with_header) {
    for (std::size_t col = 0; col < columns; ++col) {
      width[col] = std::max(width[col], head[col].size());
    }
  }
  for (const std::vector<std::string>& cells : data) {
    for (std::size_t col = 0; col < columns; ++col) {
      width[col] = std::max(width[col], cells[col].size());
    }
  }

  std::string out;
  const auto emit_row = [&](const std::vector<std::string>& cells) {
    for (std::size_t col = 0; col < columns; ++col) {
      if (md) {
        out.append(col == 0 ? "| " : " | ");
      }
      out.append(cells[col]);
      // Pad to the column width, except the last cell of an aligned row (no trailing space).
      if (md || col + 1 < columns) {
        out.append(width[col] - cells[col].size(), ' ');
      }
      if (md && col + 1 == columns) {
        out.append(" |");
      } else if (!md && col + 1 < columns) {
        out.append("  ");  // two-space column gap
      }
    }
    out.push_back('\n');
  };

  if (with_header) {
    emit_row(head);
    // The rule under the header: `| --- | --- |` for md, a dashed underline for aligned.
    for (std::size_t col = 0; col < columns; ++col) {
      if (md) {
        out.append(col == 0 ? "| " : " | ");
      }
      out.append(width[col], '-');
      if (md && col + 1 == columns) {
        out.append(" |");
      } else if (!md && col + 1 < columns) {
        out.append("  ");
      }
    }
    out.push_back('\n');
  }
  for (const std::vector<std::string>& cells : data) {
    emit_row(cells);
  }
  return out;
}

}  // namespace xff::render
