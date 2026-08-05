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

#include "xff/cli/plain_backend.h"

#include <cstddef>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "xff/cli/help_model.h"

namespace xff::cli {

std::string PlainRefLocator(const RefTarget& target) {
  switch (target.kind) {
    case RefTarget::Kind::kTopic: return "--help=" + target.id;
    case RefTarget::Kind::kManPage: return target.id + "(" + target.section + ")";
    case RefTarget::Kind::kFlag:
    case RefTarget::Kind::kPrimary:
    case RefTarget::Kind::kUrl:
    case RefTarget::Kind::kAnchor: return target.id;
  }
  return target.id;
}

std::string RenderInlinesPlain(const Inlines& runs) {
  std::string out;
  for (const Inline& run : runs) {
    // Emphasis markup drops in plain text; a ref shows its label, or its plain
    // locator when the label is empty. All other styles are literal text.
    if (run.style == Inline::Style::kRef && run.text.empty() && run.target.has_value()) {
      absl::StrAppend(&out, PlainRefLocator(*run.target));
    } else {
      absl::StrAppend(&out, run.text);
    }
  }
  return out;
}

void PlainTextBackend::Preamble(const Document& doc) {
  if (!doc.name.empty()) {
    absl::StrAppend(&out_, doc.name);
    if (!doc.tagline.empty()) {
      absl::StrAppend(&out_, " - ", doc.tagline);
    }
    absl::StrAppend(&out_, "\n");
  }
  if (!doc.usage.empty()) {
    absl::StrAppend(&out_, "\nUsage: ", doc.usage, "\n");
  }
}

void PlainTextBackend::BeginSection(const Section& section) {
  absl::StrAppend(&out_, "\n", section.title, "\n");
}

void PlainTextBackend::BeginSubsection(const Subsection& subsection) {
  absl::StrAppend(&out_, "\n  ", subsection.title, "\n");
}

void PlainTextBackend::BeginEntry(const Entry& entry) {
  absl::StrAppend(&out_, "  ", entry.term, entry.xff ? " [xff]" : "", "\n");
  if (!entry.summary.empty()) {
    absl::StrAppend(&out_, "      ", RenderInlinesPlain(entry.summary), "\n");
  }
}

void PlainTextBackend::EmitProse(const Prose& prose) {
  absl::StrAppend(&out_, RenderInlinesPlain(prose.runs), "\n");
}

void PlainTextBackend::EmitExample(const Example& example) {
  for (const std::string_view line : absl::StrSplit(example.text, '\n')) {
    absl::StrAppend(&out_, "    ", line, "\n");
  }
}

void PlainTextBackend::EmitBullets(const Bullets& bullets) {
  for (const Inlines& item : bullets.items) {
    absl::StrAppend(&out_, "  - ", RenderInlinesPlain(item), "\n");
  }
}

void PlainTextBackend::EmitRows(const Rows& rows) {
  std::size_t width = 0;
  for (const Row& row : rows.rows) {
    width = row.term.size() > width ? row.term.size() : width;
  }
  for (const Row& row : rows.rows) {
    absl::StrAppend(
        &out_, "  ", row.term, std::string(width - row.term.size(), ' '), "  ", RenderInlinesPlain(row.description),
        "\n");
  }
}

void PlainTextBackend::EmitSeeAlso(const SeeAlso& see_also) {
  absl::StrAppend(&out_, "See also:");
  std::string_view sep = " ";
  for (const RefTarget& ref : see_also.refs) {
    absl::StrAppend(&out_, sep, PlainRefLocator(ref));
    sep = ", ";
  }
  if (!see_also.note.empty()) {
    absl::StrAppend(&out_, " ", RenderInlinesPlain(see_also.note));
  }
  absl::StrAppend(&out_, "\n");
}

std::string PlainTextBackend::Take() {
  return out_;
}

}  // namespace xff::cli
