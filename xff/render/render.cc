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

#include <string>
#include <string_view>

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

}  // namespace

std::string Renderer::Record(std::string_view path, std::string_view color) const {
  switch (format_) {
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
    case Format::kJsonl:
    case Format::kNul:
    case Format::kPlain: return "";
  }
  return "";  // unreachable: every Format handled above
}

}  // namespace xff::render
