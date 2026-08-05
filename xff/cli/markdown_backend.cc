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

#include "xff/cli/markdown_backend.h"

#include <string>
#include <string_view>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "xff/cli/help_model.h"

namespace xff::cli {
namespace {

// The GitHub anchor slug for an in-document cross-reference target: lower-cased,
// leading option dashes dropped, and any run of non-alphanumerics folded to a
// single '-' (how GitHub derives heading anchors).
std::string SlugFor(const RefTarget& target) {
  std::string slug;
  bool pending_dash = false;
  for (const char chr : target.id) {
    if (absl::ascii_isalnum(chr)) {
      if (pending_dash && !slug.empty()) {
        slug.push_back('-');
      }
      pending_dash = false;
      slug.push_back(absl::ascii_tolower(chr));
    } else {
      pending_dash = true;  // dashes / other punctuation collapse; leading run is dropped
    }
  }
  return slug;
}

}  // namespace

std::string MarkdownRefLink(const RefTarget& target, const std::string& label) {
  const std::string text = label.empty() ? target.id : label;
  switch (target.kind) {
    case RefTarget::Kind::kUrl: return absl::StrCat("[", text, "](", target.id, ")");
    case RefTarget::Kind::kManPage:
      // GitHub has no man-page links; render the conventional name(section) text.
      return label.empty() ? absl::StrCat(target.id, "(", target.section, ")") : label;
    case RefTarget::Kind::kTopic:
    case RefTarget::Kind::kFlag:
    case RefTarget::Kind::kPrimary:
    case RefTarget::Kind::kAnchor: return absl::StrCat("[", text, "](#", SlugFor(target), ")");
  }
  return text;
}

std::string RenderInlinesMarkdown(const Inlines& runs) {
  std::string out;
  for (const Inline& run : runs) {
    switch (run.style) {
      case Inline::Style::kText: absl::StrAppend(&out, run.text); break;
      case Inline::Style::kCode: absl::StrAppend(&out, "`", run.text, "`"); break;
      case Inline::Style::kEmphasis: absl::StrAppend(&out, "_", run.text, "_"); break;
      case Inline::Style::kStrong: absl::StrAppend(&out, "**", run.text, "**"); break;
      case Inline::Style::kRef:
        absl::StrAppend(&out, run.target.has_value() ? MarkdownRefLink(*run.target, run.text) : run.text);
        break;
    }
  }
  return out;
}

void MarkdownBackend::Preamble(const Document& doc) {
  if (!doc.name.empty()) {
    absl::StrAppend(&out_, "# ", doc.name, "\n\n");
  }
  if (!doc.tagline.empty()) {
    absl::StrAppend(&out_, "> ", doc.tagline, "\n\n");
  }
  if (!doc.usage.empty()) {
    absl::StrAppend(&out_, "**Usage:** `", doc.usage, "`\n\n");
  }
}

void MarkdownBackend::BeginSection(const Section& section) {
  absl::StrAppend(&out_, "## ", section.title, "\n\n");
}

void MarkdownBackend::BeginSubsection(const Subsection& subsection) {
  absl::StrAppend(&out_, "### ", subsection.title, "\n\n");
}

void MarkdownBackend::BeginEntry(const Entry& entry) {
  absl::StrAppend(&out_, "**`", entry.term, "`**", entry.xff ? " _(xff)_" : "");
  if (!entry.summary.empty()) {
    absl::StrAppend(&out_, " - ", RenderInlinesMarkdown(entry.summary));
  }
  absl::StrAppend(&out_, "\n\n");
}

void MarkdownBackend::EmitProse(const Prose& prose) {
  absl::StrAppend(&out_, RenderInlinesMarkdown(prose.runs), "\n\n");
}

void MarkdownBackend::EmitExample(const Example& example) {
  absl::StrAppend(&out_, "```", example.lang, "\n", example.text, "\n```\n\n");
}

void MarkdownBackend::EmitBullets(const Bullets& bullets) {
  for (const Inlines& item : bullets.items) {
    absl::StrAppend(&out_, "- ", RenderInlinesMarkdown(item), "\n");
  }
  absl::StrAppend(&out_, "\n");
}

void MarkdownBackend::EmitRows(const Rows& rows) {
  absl::StrAppend(&out_, "|  |  |\n| :-- | :-- |\n");
  for (const Row& row : rows.rows) {
    absl::StrAppend(&out_, "| `", row.term, "` | ", RenderInlinesMarkdown(row.description), " |\n");
  }
  absl::StrAppend(&out_, "\n");
}

void MarkdownBackend::EmitSeeAlso(const SeeAlso& see_also) {
  absl::StrAppend(&out_, "**See also:**");
  std::string_view sep = " ";
  for (const RefTarget& ref : see_also.refs) {
    absl::StrAppend(&out_, sep, MarkdownRefLink(ref, /*label=*/""));
    sep = ", ";
  }
  if (!see_also.note.empty()) {
    absl::StrAppend(&out_, " ", RenderInlinesMarkdown(see_also.note));
  }
  absl::StrAppend(&out_, "\n\n");
}

std::string MarkdownBackend::Take() {
  // Every block emits a trailing blank line for separation; collapse the run at the
  // document end to a single newline so the output ends cleanly.
  std::string out = out_;
  while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') {
    out.pop_back();
  }
  return out;
}

}  // namespace xff::cli
