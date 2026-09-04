// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_CLI_HTML_BACKEND_H_
#define XFF_CLI_HTML_BACKEND_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

#include "xff/cli/help_backend.h"
#include "xff/cli/help_model.h"

namespace xff::cli {

// A self-contained HTML5 backend for the semantic help model. The result has no
// external assets or scripts, so it can be saved, published, or viewed offline.
class HtmlBackend final : public HelpBackend {
 public:
  void Preamble(const Document& doc) override;
  void BeginSection(const Section& section) override;
  void EndSection(const Section& section) override;
  void BeginSubsection(const Subsection& subsection) override;
  void EndSubsection(const Subsection& subsection) override;
  void BeginEntry(const Entry& entry) override;
  void EndEntry(const Entry& entry) override;
  void EmitProse(const Prose& prose) override;
  void EmitExample(const Example& example) override;
  void EmitBullets(const Bullets& bullets) override;
  void EmitRows(const Rows& rows) override;
  void EmitTable(const Table& table) override;
  void EmitSeeAlso(const SeeAlso& see_also) override;
  [[nodiscard]] std::string Take() override;

 private:
  std::string UniqueAnchor(std::string_view explicit_anchor, std::string_view fallback);

  std::string out_;
  std::unordered_map<std::string, std::size_t> anchor_counts_;
};

// Escapes text for HTML character data or a quoted attribute respectively.
[[nodiscard]] std::string HtmlEscape(std::string_view text);
[[nodiscard]] std::string HtmlAttributeEscape(std::string_view text);

// Converts the stable model identifier spelling to the anchor spelling used by
// the HTML document and its internal references.
[[nodiscard]] std::string HtmlSlug(std::string_view text);

[[nodiscard]] std::string RenderInlinesHtml(const Inlines& runs);
[[nodiscard]] std::string HtmlRefLink(const RefTarget& target, std::string_view label);

}  // namespace xff::cli

#endif  // XFF_CLI_HTML_BACKEND_H_
