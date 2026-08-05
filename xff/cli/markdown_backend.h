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

#ifndef XFF_CLI_MARKDOWN_BACKEND_H_
#define XFF_CLI_MARKDOWN_BACKEND_H_

#include <string>

#include "xff/cli/help_backend.h"
#include "xff/cli/help_model.h"

// The Markdown help backend (#154): renders the help model to GitHub-flavored
// Markdown. Inline emphasis maps to Markdown (`code`, _italic_, **bold**); a
// cross-reference becomes a real link where it can - a URL link, or an in-document
// anchor for a topic / flag / primary / anchor target - and plain text for a man
// page (GitHub has no man links). Examples become fenced code blocks carrying
// their info string. Provisional layout until it renders real help data (the
// byte-identical --markdown port is a later slice).
namespace xff::cli {

// A HelpBackend that accumulates Markdown. Construct, RenderDocument() into it, Take().
class MarkdownBackend final : public HelpBackend {
 public:
  void Preamble(const Document& doc) override;
  void BeginSection(const Section& section) override;
  void BeginSubsection(const Subsection& subsection) override;
  void BeginEntry(const Entry& entry) override;
  void EmitProse(const Prose& prose) override;
  void EmitExample(const Example& example) override;
  void EmitBullets(const Bullets& bullets) override;
  void EmitRows(const Rows& rows) override;
  void EmitSeeAlso(const SeeAlso& see_also) override;
  [[nodiscard]] std::string Take() override;

 private:
  std::string out_;
};

// Renders inline runs to Markdown: emphasis mapped to Markdown markup and each
// cross-reference to a link (or plain text for a man page). Exposed for testing.
[[nodiscard]] std::string RenderInlinesMarkdown(const Inlines& runs);

// The Markdown form of a cross-reference: `[label](url)` for a URL, `[label](#slug)`
// for an in-document target, and `label` (e.g. `find(1)`) for a man page. Exposed
// for testing.
[[nodiscard]] std::string MarkdownRefLink(const RefTarget& target, const std::string& label);

}  // namespace xff::cli

#endif  // XFF_CLI_MARKDOWN_BACKEND_H_
