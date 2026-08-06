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

#ifndef XFF_CLI_PLAIN_BACKEND_H_
#define XFF_CLI_PLAIN_BACKEND_H_

#include <cstddef>
#include <string>

#include "xff/cli/help_backend.h"
#include "xff/cli/help_model.h"

// The plain-text help backend: renders the help model to unadorned terminal text -
// the `--help` / topic format. Blocks are separated by a blank line; a section
// heading is upper-cased, a subsection ends in a colon, an entry is a term line
// with an indented summary, and a vocabulary table aligns on its widest term. Prose
// drops backtick markup, while a term / row description keeps it (matching the
// hand-written help). Flowing text (prose, a bullet, an entry summary / detail, a
// see-also note) word-wraps to the configured `width` (#153 / #164), each
// continuation line carrying its block's indent; an example block and an aligned
// row table keep their verbatim layout. A `width` of 0 disables wrapping. ANSI
// color is a later slice (the color backend).
namespace xff::cli {

// A HelpBackend that accumulates plain text. Construct (optionally with a wrap
// width - 0 means do not wrap), RenderDocument() into it, then Take() the result.
class PlainTextBackend final : public HelpBackend {
 public:
  PlainTextBackend() = default;

  explicit PlainTextBackend(std::size_t width) : width_(width) {}

  void Preamble(const Document& doc) override;
  void BeginSection(const Section& section) override;
  void BeginSubsection(const Subsection& subsection) override;
  void BeginEntry(const Entry& entry) override;
  void EndEntry(const Entry& entry) override;
  void EmitProse(const Prose& prose) override;
  void EmitExample(const Example& example) override;
  void EmitBullets(const Bullets& bullets) override;
  void EmitRows(const Rows& rows) override;
  void EmitSeeAlso(const SeeAlso& see_also) override;
  [[nodiscard]] std::string Take() override;

 private:
  // Separates blocks: emits a blank line before the next block unless at the start.
  void StartBlock();

  std::string out_;
  std::size_t width_ = 0;  // wrap column for flowing text; 0 disables wrapping (see wrap.h)
  bool in_entry_ = false;  // an entry's detail prose renders indented under its term, not as free prose
};

// Renders inline runs to plain text: literal content with emphasis markup dropped
// and each cross-reference shown as its plain locator. Exposed for reuse / testing.
[[nodiscard]] std::string RenderInlinesPlain(const Inlines& runs);

// The plain locator a cross-reference resolves to in text output (e.g. a kManPage
// target -> "find(1)", a kTopic -> "--help=fields"). Exposed for reuse / testing.
[[nodiscard]] std::string PlainRefLocator(const RefTarget& target);

}  // namespace xff::cli

#endif  // XFF_CLI_PLAIN_BACKEND_H_
