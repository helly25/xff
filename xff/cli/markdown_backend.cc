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

#include <cstddef>
#include <string>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "xff/cli/help_model.h"

namespace xff::cli {
namespace {

// The GitHub anchor slug for an in-document cross-reference: lower-cased, leading
// option dashes dropped, and any run of non-alphanumerics folded to a single '-'.
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
      pending_dash = true;
    }
  }
  return slug;
}

}  // namespace

std::string MarkdownRefLink(const RefTarget& target, const std::string& label) {
  // STYLE_CPP: a by-value std::string_view is never const (const only disables its own API).
  // NOLINTNEXTLINE(misc-const-correctness)
  std::string_view text = label.empty() ? target.id : label;
  switch (target.kind) {
    case RefTarget::Kind::kUrl: return absl::StrCat("[", text, "](", target.id, ")");
    case RefTarget::Kind::kManPage: return label.empty() ? absl::StrCat(target.id, "(", target.section, ")") : label;
    case RefTarget::Kind::kTopic:
    case RefTarget::Kind::kFlag:
    case RefTarget::Kind::kPrimary:
    case RefTarget::Kind::kAnchor: return absl::StrCat("[", text, "](#", SlugFor(target), ")");
  }
  return std::string(text);
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
  absl::StrAppendFormat(&out_, "# %s\n\n%s.\n\n**Usage:** `%s %s`\n", doc.name, doc.tagline, doc.name, doc.usage);
}

void MarkdownBackend::BeginSection(const Section& section) {
  absl::StrAppendFormat(&out_, "\n## %s\n", section.title);
}

void MarkdownBackend::BeginSubsection(const Subsection& subsection) {
  if (subsection.title.empty()) {
    return;  // a title-less subsection only groups/indents in plain text; no heading here
  }
  absl::StrAppendFormat(&out_, "\n### %s\n", subsection.title);
}

void MarkdownBackend::BeginEntry(const Entry& entry) {
  // A term is backtick-wrapped so its `=NAME` / `[..]` / `|` stay literal.
  std::string tag;
  if (!entry.tags.empty()) {
    tag = absl::StrCat(" _(", absl::StrJoin(entry.tags, ", "), ")_");
  } else if (entry.xff) {
    tag = " _(xff)_";
  }
  absl::StrAppend(&out_, "- `", entry.term, "` - ", RenderInlinesMarkdown(entry.summary), tag, "\n");
  in_entry_ = true;
}

void MarkdownBackend::EndEntry(const Entry& /*entry*/) {
  in_entry_ = false;
}

void MarkdownBackend::EmitProse(const Prose& prose) {
  if (in_entry_) {
    absl::StrAppend(&out_, "  ", RenderInlinesMarkdown(prose.runs), "\n");  // indented bullet continuation
  } else {
    absl::StrAppend(&out_, "\n", RenderInlinesMarkdown(prose.runs), "\n");
  }
}

void MarkdownBackend::EmitExample(const Example& example) {
  absl::StrAppend(&out_, "\n```", example.lang, "\n", example.text, "\n```\n");
}

void MarkdownBackend::EmitBullets(const Bullets& bullets) {
  absl::StrAppend(&out_, "\n");
  for (const Inlines& item : bullets.items) {
    absl::StrAppend(&out_, "- ", RenderInlinesMarkdown(item), "\n");
  }
}

void MarkdownBackend::EmitRows(const Rows& rows) {
  // Inside a flag entry the rows are a value table nested under the flag's bullet: indent
  // them to match the entry's 2-space continuation prose, and close with a blank line so
  // the following prose is not absorbed as a lazy continuation of the last list item.
  const std::string prefix = in_entry_ ? "  - `" : "- `";
  absl::StrAppend(&out_, "\n");
  for (const Row& row : rows.rows) {
    absl::StrAppend(&out_, prefix, row.term, "` - ", RenderInlinesMarkdown(row.description), "\n");
  }
  if (in_entry_) {
    absl::StrAppend(&out_, "\n");
  }
}

void MarkdownBackend::EmitSeeAlso(const SeeAlso& see_also) {
  absl::StrAppend(&out_, "\n");
  for (std::size_t i = 0; i < see_also.refs.size(); ++i) {
    const RefTarget& ref = see_also.refs[i];
    absl::StrAppendFormat(&out_, "%s`%s`(%s)", i == 0 ? "" : ", ", ref.id, ref.section);
  }
  absl::StrAppend(&out_, "\n");
  if (!see_also.note.empty()) {
    absl::StrAppend(&out_, "\n", RenderInlinesMarkdown(see_also.note), "\n");
  }
}

std::string MarkdownBackend::Take() {
  return std::move(out_);
}

}  // namespace xff::cli
