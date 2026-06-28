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

}  // namespace

std::string Renderer::Record(std::string_view path) const {
  switch (format_) {
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
      if (encoding_ == PathEncoding::kEscape) {
        std::string record;
        AppendCEscaped(path, &record);
        record.push_back('\n');
        return record;
      }
      return absl::StrCat(path, "\n");
    }
  }
  return absl::StrCat(path, "\n");  // unreachable: every Format returns above
}

}  // namespace xff::render
