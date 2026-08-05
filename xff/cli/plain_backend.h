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

#include <string>

#include "xff/cli/help_backend.h"
#include "xff/cli/help_model.h"

// The plain-text help backend (#154 slice C): renders the help model to
// unadorned terminal text. Inline emphasis (code / italic / bold) drops its
// markup; a cross-reference renders as its plain locator (`--help=fields`,
// `find(1)`, a URL). Free-flow width control and ANSI color are later slices
// (#153 / the color backend); this backend emits one prose paragraph per line.
namespace xff::cli {

// A HelpBackend that accumulates plain text. Construct, RenderDocument() into it,
// then Take() the result.
class PlainTextBackend final : public HelpBackend {
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

// Renders inline runs to plain text: literal content with emphasis markup dropped
// and each cross-reference shown as its plain locator. Exposed for reuse / testing.
[[nodiscard]] std::string RenderInlinesPlain(const Inlines& runs);

// The plain locator a cross-reference resolves to in text output (e.g. a kManPage
// target -> "find(1)", a kTopic -> "--help=fields"). Exposed for reuse / testing.
[[nodiscard]] std::string PlainRefLocator(const RefTarget& target);

}  // namespace xff::cli

#endif  // XFF_CLI_PLAIN_BACKEND_H_
