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

#ifndef XFF_CLI_PLAIN_HELP_H_
#define XFF_CLI_PLAIN_HELP_H_

#include <string>
#include <string_view>

#include "absl/types/span.h"
#include "xff/cli/doc_renderer.h"

namespace xff::cli {

// Renders the shared reference (or any single section such as WriteFields) as xff's plain-help
// text, matching the house style of the other `--help` / `--help=TOPIC` output: UPPERCASE
// top-level headings, `Title:` subsection headers, and 2-space-indented aligned term columns.
// The third concrete DocRenderer alongside RoffRenderer (--man) and MarkdownRenderer (--markdown),
// so the plain `--help=fields` topic reuses the one WriteFields() walk instead of hand-mirroring it.
// Inline `code` spans have their backticks dropped (the header's ScanInline contract for plain).
class PlainRenderer final : public DocRenderer {
 public:
  void Document(std::string_view name, std::string_view tagline, std::string_view usage) override;
  void Section(std::string_view title) override;
  void Subsection(std::string_view title) override;
  void Prose(std::string_view text) override;
  void Bullets(absl::Span<const std::string_view> items) override;
  void Entry(std::string_view term, std::string_view summary, std::string_view details, bool xff) override;
  void Rows(absl::Span<const DocRow> rows) override;
  void Example(std::string_view text) override;
  void SeeAlso(absl::Span<const CrossRef> refs, std::string_view note) override;
  [[nodiscard]] std::string Take() override;

 private:
  // Separates a new block from the preceding one with a single blank line (a no-op at the start of
  // the document). Every block-level method emits exactly one trailing newline, so this yields
  // exactly one blank line between blocks.
  void StartBlock();
  // Appends `text`, dropping the backticks around inline `code` spans (the plain-format mapping).
  void EmitInline(std::string_view text);

  std::string out_;
};

}  // namespace xff::cli

#endif  // XFF_CLI_PLAIN_HELP_H_
