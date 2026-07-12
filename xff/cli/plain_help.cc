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

#include "xff/cli/plain_help.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "xff/cli/doc_renderer.h"
#include "xff/cli/help.h"

namespace xff::cli {

void PlainRenderer::Document(std::string_view name, std::string_view tagline, std::string_view usage) {
  StartBlock();
  absl::StrAppend(&out_, name, " - ", tagline, "\n");
  absl::StrAppend(&out_, "\nUsage: ", name, " ", usage, "\n");
}

void PlainRenderer::Section(std::string_view title) {
  // House style: top-level headings are uppercase (like PRINTF DIRECTIVES / TIME FORMATS).
  StartBlock();
  absl::StrAppend(&out_, absl::AsciiStrToUpper(title), "\n");
}

void PlainRenderer::Subsection(std::string_view title) {
  StartBlock();
  absl::StrAppend(&out_, title, ":\n");
}

void PlainRenderer::Prose(std::string_view text) {
  StartBlock();
  EmitInline(text);
  absl::StrAppend(&out_, "\n");
}

void PlainRenderer::Bullets(absl::Span<const std::string_view> items) {
  // Glue the list directly under its subsection heading (no leading blank line), 2-space indent.
  for (const std::string_view item : items) {
    absl::StrAppend(&out_, "  - ");
    EmitInline(item);
    absl::StrAppend(&out_, "\n");
  }
}

void PlainRenderer::Entry(std::string_view term, std::string_view summary, std::string_view details, bool xff) {
  StartBlock();
  absl::StrAppend(&out_, term, xff ? "  (xff)" : "", "\n");
  absl::StrAppend(&out_, "    ", summary, "\n");
  if (!details.empty()) {
    absl::StrAppend(&out_, "    ", details, "\n");
  }
}

void PlainRenderer::Rows(absl::Span<const DocRow> rows) {
  // Reuse the shared {term, description} layout so the fields rows align exactly like the
  // --help=printf / --help=time / --help=size vocabularies (2-space indent, widest term + 2).
  const std::vector<DocRow> copy(rows.begin(), rows.end());
  absl::StrAppend(&out_, RenderDocRows("  ", copy));
}

void PlainRenderer::Example(std::string_view text) {
  StartBlock();
  absl::StrAppend(&out_, text);
  if (text.empty() || text.back() != '\n') {
    absl::StrAppend(&out_, "\n");
  }
}

void PlainRenderer::SeeAlso(absl::Span<const CrossRef> refs, std::string_view note) {
  StartBlock();
  for (std::size_t i = 0; i < refs.size(); ++i) {
    absl::StrAppend(&out_, i == 0 ? "" : ", ", refs[i].name, "(", refs[i].section, ")");
  }
  absl::StrAppend(&out_, "\n");
  if (!note.empty()) {
    StartBlock();
    EmitInline(note);
    absl::StrAppend(&out_, "\n");
  }
}

std::string PlainRenderer::Take() {
  return std::move(out_);
}

void PlainRenderer::StartBlock() {
  if (!out_.empty()) {
    absl::StrAppend(&out_, "\n");
  }
}

void PlainRenderer::EmitInline(std::string_view text) {
  ScanInline(
      text, [&](std::string_view run) { absl::StrAppend(&out_, run); },
      [&](std::string_view code) { absl::StrAppend(&out_, code); });
}

}  // namespace xff::cli
