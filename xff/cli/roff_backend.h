// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
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

#ifndef XFF_CLI_ROFF_BACKEND_H_
#define XFF_CLI_ROFF_BACKEND_H_

#include <string>
#include <string_view>

#include "xff/cli/help_backend.h"
#include "xff/cli/help_model.h"

// The roff (man-page) help backend (#154): renders the help model to man-page
// source. Section titles are upper-cased (man convention); inline code / strong
// become \fB..\fR, emphasis \fI..\fR; a cross-reference renders as its plain
// locator. This is the model counterpart of manpage.cc's RoffRenderer. Unlike that
// walk it renders entry details through the model (so backticks in a flag's detail
// become bold rather than literal); wiring --man onto it therefore regenerates the
// man golden - a later slice, gated on the output-format decision.
namespace xff::cli {

// A HelpBackend that accumulates roff source. Construct, RenderDocument(), Take().
class RoffBackend final : public HelpBackend {
 public:
  void Preamble(const Document& doc) override;
  void BeginSection(const Section& section) override;
  void BeginSubsection(const Subsection& subsection) override;
  void BeginEntry(const Entry& entry) override;
  void EmitProse(const Prose& prose) override;
  void EmitExample(const Example& example) override;
  void EmitBullets(const Bullets& bullets) override;
  void EmitRows(const Rows& rows) override;

  void EmitTable(const Table& table) override;
  void EmitSeeAlso(const SeeAlso& see_also) override;
  [[nodiscard]] std::string Take() override;

 private:
  void EmitInline(const Inlines& runs);

  std::string out_;
  bool para_ = false;  // a prior paragraph is open, so the next prose needs a .PP separator
};

// Escapes text for a roff source line (backslash, option hyphens, leading control
// characters). Exposed for testing.
[[nodiscard]] std::string RoffEscape(std::string_view text);

}  // namespace xff::cli

#endif  // XFF_CLI_ROFF_BACKEND_H_
