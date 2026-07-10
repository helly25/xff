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

#include "xff/cli/manpage.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "xff/cli/doc_renderer.h"

namespace xff::cli {
namespace {

// Escape text for a roff source line: backslash starts an escape, a hyphen is rendered as `\-`
// so option dashes are real hyphen-minus (copy-pasteable), and a leading `.` or `'` in the first
// column - a roff control line - is guarded with the zero-width `\&` (prose, details, and
// verbatim examples may begin a line with either).
std::string Roff(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  bool at_line_start = true;
  for (const char ch : text) {
    if (at_line_start && (ch == '.' || ch == '\'')) {
      out += "\\&";
    }
    at_line_start = ch == '\n';
    if (ch == '\\') {
      out += "\\\\";
    } else if (ch == '-') {
      out += "\\-";
    } else {
      out += ch;
    }
  }
  return out;
}

// Renders the shared reference (WriteReference) as roff man-page source. Inline `code` spans
// become \fB..\fR; the `.PP` state gives consecutive prose paragraphs a blank line between them.
class RoffRenderer final : public DocRenderer {
 public:
  void Document(std::string_view name, std::string_view tagline, std::string_view usage) override {
    absl::StrAppendFormat(&out_, ".TH %s 1 \"\" \"%s\" \"User Commands\"\n", Roff(name), Roff(name));
    absl::StrAppend(&out_, ".SH NAME\n", Roff(name), " \\- ", Roff(tagline), "\n");
    absl::StrAppend(&out_, ".SH SYNOPSIS\n.B ", Roff(name), "\n", Roff(usage), "\n");
    para_ = false;
  }

  void Section(std::string_view title) override {
    // Man convention: top-level section headings are uppercase. The shared walk carries
    // natural-case titles (so Markdown / plain render them as authored); roff uppercases here.
    absl::StrAppendFormat(&out_, ".SH %s\n", Roff(absl::AsciiStrToUpper(title)));
    para_ = false;
  }

  void Subsection(std::string_view title) override {
    absl::StrAppendFormat(&out_, ".SS %s\n", Roff(title));
    para_ = false;
  }

  void Prose(std::string_view text) override {
    if (para_) {
      absl::StrAppend(&out_, ".PP\n");
    }
    EmitInline(text);
    absl::StrAppend(&out_, "\n");
    para_ = true;
  }

  void Bullets(absl::Span<const std::string_view> items) override {
    for (const std::string_view item : items) {
      absl::StrAppend(&out_, ".IP \\(bu 3\n");
      EmitInline(item);
      absl::StrAppend(&out_, "\n");
    }
    para_ = false;
  }

  void Entry(std::string_view term, std::string_view summary, std::string_view details, bool xff) override {
    absl::StrAppendFormat(&out_, ".TP\n.B %s\n%s%s\n", Roff(term), Roff(summary), xff ? " (xff extension)" : "");
    if (!details.empty()) {
      absl::StrAppendFormat(&out_, ".sp\n%s\n", Roff(details));
    }
    para_ = false;
  }

  void Rows(absl::Span<const DocRow> rows) override {
    for (const DocRow& row : rows) {
      absl::StrAppendFormat(&out_, ".TP\n.B %s\n%s\n", Roff(row.first), Roff(row.second));
    }
    para_ = false;
  }

  void Example(std::string_view text) override {
    absl::StrAppend(&out_, ".PP\n.nf\n", Roff(text));
    if (text.empty() || text.back() != '\n') {
      absl::StrAppend(&out_, "\n");
    }
    absl::StrAppend(&out_, ".fi\n");
    para_ = false;
  }

  void SeeAlso(absl::Span<const CrossRef> refs, std::string_view note) override {
    for (std::size_t i = 0; i < refs.size(); ++i) {
      absl::StrAppendFormat(
          &out_, ".BR %s (%s)%s\n", Roff(refs[i].name), Roff(refs[i].section), i + 1 < refs.size() ? "," : "");
    }
    if (!note.empty()) {
      absl::StrAppend(&out_, ".PP\n");
      EmitInline(note);
      absl::StrAppend(&out_, "\n");
    }
    para_ = false;
  }

  std::string Take() override { return std::move(out_); }

 private:
  // Emit prose / a bullet, mapping backtick `code` spans to bold (\fB..\fR).
  void EmitInline(std::string_view text) {
    ScanInline(
        text, [&](std::string_view run) { absl::StrAppend(&out_, Roff(run)); },
        [&](std::string_view code) { absl::StrAppend(&out_, "\\fB", Roff(code), "\\fR"); });
  }

  std::string out_;
  bool para_ = false;
};

}  // namespace

std::string ManPage() {
  RoffRenderer renderer;
  WriteReference(renderer);
  return renderer.Take();
}

}  // namespace xff::cli
