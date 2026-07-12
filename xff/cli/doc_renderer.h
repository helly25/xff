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

#ifndef XFF_CLI_DOC_RENDERER_H_
#define XFF_CLI_DOC_RENDERER_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "absl/types/span.h"

namespace xff::cli {

// A {term, description} row - the printf / time / size / field vocabularies are lists of
// these. The engine / datetime doc sources already return exactly this pair type.
using DocRow = std::pair<std::string_view, std::string_view>;

// A SEE ALSO cross reference: a related manual page, `name(section)`.
struct CrossRef {
  std::string_view name;
  std::string_view section;
};

// The structural vocabulary of the generated reference. `WriteReference()` walks the single
// source of truth (global flags + the expression registry + the in_full help topics) exactly
// once and drives one of these; each concrete renderer (roff man page, Markdown, plain help)
// emits its own format natively - owning its own escaping - so the outputs cannot drift from
// one another or from the SOT. Every method appends to an internal buffer returned by Take().
//
// Inline emphasis: prose and notes may carry backtick-delimited `code` spans; ScanInline()
// below is the shared tokenizer so each renderer maps a span to its own emphasis (roff
// \fB..\fR, Markdown keeps the backticks, plain drops them) from one authored syntax.
class DocRenderer {
 public:
  DocRenderer() = default;
  virtual ~DocRenderer() = default;
  DocRenderer(const DocRenderer&) = delete;
  DocRenderer& operator=(const DocRenderer&) = delete;
  DocRenderer(DocRenderer&&) = delete;
  DocRenderer& operator=(DocRenderer&&) = delete;

  // The document preamble: program name, one-line tagline, and the usage synopsis.
  virtual void Document(std::string_view name, std::string_view tagline, std::string_view usage) = 0;
  // A top-level section (roff .SH / Markdown ## / plain heading).
  virtual void Section(std::string_view title) = 0;
  // A subsection within a section (roff .SS / Markdown ### / group header).
  virtual void Subsection(std::string_view title) = 0;
  // A prose paragraph (may carry `code` spans).
  virtual void Prose(std::string_view text) = 0;
  // An unordered bullet list (each item may carry `code` spans). Free to interleave with
  // Prose and Rows - prose, then bullets, then more prose is just three sequential calls.
  virtual void Bullets(absl::Span<const std::string_view> items) = 0;
  // A definition entry: the term (flag display or primary synopsis), its one-line summary,
  // the optional longer details, and whether it is an xff-only extension.
  virtual void Entry(std::string_view term, std::string_view summary, std::string_view details, bool xff) = 0;
  // An aligned {term, description} vocabulary list (printf / time / size / fields).
  virtual void Rows(absl::Span<const DocRow> rows) = 0;
  // A preformatted, verbatim block (worked examples); no fill or wrapping.
  virtual void Example(std::string_view text) = 0;
  // The SEE ALSO cross references plus a trailing prose note (which may carry `code` spans).
  virtual void SeeAlso(absl::Span<const CrossRef> refs, std::string_view note) = 0;

  // The accumulated document.
  [[nodiscard]] virtual std::string Take() = 0;
};

// Splits `text` into literal runs and backtick-delimited `code` spans, invoking `on_text` for
// each literal run and `on_code` for the inside of each span. An unterminated backtick is
// treated literally. The shared inline-markup convention so every renderer maps emphasis from
// the same authored source syntax.
template<typename OnText, typename OnCode>
void ScanInline(std::string_view text, OnText on_text, OnCode on_code) {
  std::size_t pos = 0;
  while (pos < text.size()) {
    const std::size_t open = text.find('`', pos);
    if (open == std::string_view::npos) {
      on_text(text.substr(pos));
      return;
    }
    if (open > pos) {
      on_text(text.substr(pos, open - pos));
    }
    const std::size_t close = text.find('`', open + 1);
    if (close == std::string_view::npos) {
      on_text(text.substr(open));  // unterminated: the backtick is literal
      return;
    }
    on_code(text.substr(open + 1, close - open - 1));
    pos = close + 1;
  }
}

// Parses a small Markdown subset and drives `out`'s primitives so authored prose reads
// naturally at the call site and every format renders it natively. Understood: blank lines
// separate paragraphs (Prose); a run of lines each starting `- ` (or `* `) is one bullet list
// (Bullets); inline `code` spans are handled by the primitives via ScanInline. Everything else
// is paragraph text. This is the shared "the renderer understands `-` starts a bullet" pass.
void WriteMarkdown(DocRenderer& out, std::string_view block);

// The single traversal of the SOT that produces the complete reference. Adding a primary, a
// global flag, or an in_full topic flows to every output format through this one function.
void WriteReference(DocRenderer& out);

// Emits just the FIELDS section (the `{field}` placeholder vocabulary) through `out`. Part of
// WriteReference()'s walk, and also driven on its own by the plain `--help=fields` topic (via a
// PlainRenderer), so the topic and the man / Markdown / full reference share this one source.
void WriteFields(DocRenderer& out);

}  // namespace xff::cli

#endif  // XFF_CLI_DOC_RENDERER_H_
