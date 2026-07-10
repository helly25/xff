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

#include "xff/cli/markdown.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "xff/cli/doc_renderer.h"

namespace xff::cli {
namespace {

// Renders the shared reference (WriteReference) as GitHub-flavored Markdown. Prose already
// carries the authored backtick `code` spans, so it passes through verbatim; a term is wrapped
// in backticks so its `=NAME` / `[..]` / `|` stay literal, and xff-only items get an _(xff)_ tag.
class MarkdownRenderer final : public DocRenderer {
 public:
  void Document(std::string_view name, std::string_view tagline, std::string_view usage) override {
    absl::StrAppendFormat(&out_, "# %s\n\n%s.\n\n**Usage:** `%s %s`\n", name, tagline, name, usage);
  }

  void Section(std::string_view title) override { absl::StrAppendFormat(&out_, "\n## %s\n", title); }

  void Subsection(std::string_view title) override { absl::StrAppendFormat(&out_, "\n### %s\n", title); }

  void Prose(std::string_view text) override { absl::StrAppend(&out_, "\n", text, "\n"); }

  void Bullets(absl::Span<const std::string_view> items) override {
    absl::StrAppend(&out_, "\n");
    for (const std::string_view item : items) {
      absl::StrAppend(&out_, "- ", item, "\n");
    }
  }

  void Entry(std::string_view term, std::string_view summary, std::string_view details, bool xff) override {
    absl::StrAppendFormat(&out_, "- `%s` - %s%s\n", term, summary, xff ? " _(xff)_" : "");
    if (!details.empty()) {
      absl::StrAppend(&out_, "  ", details, "\n");  // indented continuation of the bullet
    }
  }

  void Rows(absl::Span<const DocRow> rows) override {
    absl::StrAppend(&out_, "\n");
    for (const DocRow& row : rows) {
      absl::StrAppendFormat(&out_, "- `%s` - %s\n", row.first, row.second);
    }
  }

  void Example(std::string_view text) override { absl::StrAppend(&out_, "\n```\n", text, "\n```\n"); }

  void SeeAlso(absl::Span<const CrossRef> refs, std::string_view note) override {
    absl::StrAppend(&out_, "\n");
    for (std::size_t i = 0; i < refs.size(); ++i) {
      absl::StrAppendFormat(&out_, "%s`%s`(%s)", i == 0 ? "" : ", ", refs[i].name, refs[i].section);
    }
    absl::StrAppend(&out_, "\n");
    if (!note.empty()) {
      absl::StrAppend(&out_, "\n", note, "\n");
    }
  }

  std::string Take() override { return std::move(out_); }

 private:
  std::string out_;
};

}  // namespace

std::string MarkdownReference() {
  MarkdownRenderer renderer;
  WriteReference(renderer);
  return renderer.Take();
}

}  // namespace xff::cli
