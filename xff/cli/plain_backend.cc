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
#include <utility>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "xff/cli/help.h"
#include "xff/cli/help_model.h"
#include "xff/cli/wrap.h"

namespace xff::cli {
namespace {

// Reconstructs the authored inline string, keeping `code` backticks - a term, a row
// description, and an entry's summary / detail pass through with their markup (as the
// hand-written plain help did), unlike free prose which drops it.
std::string RenderInlinesRaw(const Inlines& runs) {
  std::string out;
  for (const Inline& run : runs) {
    if (run.style == Inline::Style::kCode) {
      absl::StrAppend(&out, "`", run.text, "`");
    } else {
      absl::StrAppend(&out, run.text);
    }
  }
  return out;
}

}  // namespace

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
    // Free prose drops emphasis markup; a ref shows its label, or its plain locator
    // when the label is empty. All other styles are literal text.
    if (run.style == Inline::Style::kRef && run.text.empty() && run.target.has_value()) {
      absl::StrAppend(&out, PlainRefLocator(*run.target));
    } else {
      absl::StrAppend(&out, run.text);
    }
  }
  return out;
}

void PlainTextBackend::StartBlock() {
  if (!out_.empty()) {
    absl::StrAppend(&out_, "\n");
  }
}

void PlainTextBackend::Preamble(const Document& doc) {
  if (doc.name.empty()) {
    return;  // a section-only render (e.g. a single help topic) carries no preamble
  }
  StartBlock();
  absl::StrAppend(&out_, doc.name, " - ", doc.tagline, "\n");
  absl::StrAppend(&out_, "\nUsage: ", doc.name, " ", doc.usage, "\n");
}

void PlainTextBackend::BeginSection(const Section& section) {
  // House style: top-level headings are upper-cased (PRINTF DIRECTIVES / TIME FORMATS).
  StartBlock();
  absl::StrAppend(&out_, absl::AsciiStrToUpper(section.title), "\n");
}

void PlainTextBackend::BeginSubsection(const Subsection& subsection) {
  StartBlock();
  absl::StrAppend(&out_, subsection.title, ":\n");
}

void PlainTextBackend::BeginEntry(const Entry& entry) {
  StartBlock();
  absl::StrAppend(&out_, entry.term, entry.xff ? "  (xff)" : "", "\n");
  absl::StrAppend(&out_, WrapText(RenderInlinesRaw(entry.summary), width_, "    ", "    "));
  in_entry_ = true;
}

void PlainTextBackend::EndEntry(const Entry& /*entry*/) {
  in_entry_ = false;
}

void PlainTextBackend::EmitProse(const Prose& prose) {
  if (in_entry_) {
    // An entry's detail line, indented under its term (keeps `code` markup, no blank).
    absl::StrAppend(&out_, WrapText(RenderInlinesRaw(prose.runs), width_, "    ", "    "));
    return;
  }
  StartBlock();
  absl::StrAppend(&out_, WrapText(RenderInlinesPlain(prose.runs), width_, "", ""));
}

void PlainTextBackend::EmitExample(const Example& example) {
  StartBlock();
  absl::StrAppend(&out_, example.text);
  if (example.text.empty() || example.text.back() != '\n') {
    absl::StrAppend(&out_, "\n");
  }
}

void PlainTextBackend::EmitBullets(const Bullets& bullets) {
  // Glued directly under its subsection heading (no leading blank line), 2-space indent.
  for (const Inlines& item : bullets.items) {
    absl::StrAppend(&out_, WrapText(RenderInlinesPlain(item), width_, "  - ", "    "));
  }
}

void PlainTextBackend::EmitRows(const Rows& rows) {
  // The shared {term, description} layout (2-space indent, widest term + 2), so a
  // vocabulary table aligns exactly like the --help=printf / time / size ones.
  std::vector<std::string> descriptions;
  descriptions.reserve(rows.rows.size());
  for (const Row& row : rows.rows) {
    descriptions.push_back(RenderInlinesRaw(row.description));
  }
  std::vector<std::pair<std::string_view, std::string_view>> doc_rows;
  doc_rows.reserve(rows.rows.size());
  for (std::size_t i = 0; i < rows.rows.size(); ++i) {
    doc_rows.emplace_back(rows.rows[i].term, descriptions[i]);
  }
  absl::StrAppend(&out_, RenderDocRows("  ", doc_rows));
}

void PlainTextBackend::EmitSeeAlso(const SeeAlso& see_also) {
  StartBlock();
  std::string_view sep;
  for (const RefTarget& ref : see_also.refs) {
    absl::StrAppend(&out_, sep, ref.id, "(", ref.section, ")");
    sep = ", ";
  }
  absl::StrAppend(&out_, "\n");
  if (!see_also.note.empty()) {
    StartBlock();
    absl::StrAppend(&out_, WrapText(RenderInlinesPlain(see_also.note), width_, "", ""));
  }
}

std::string PlainTextBackend::Take() {
  return std::move(out_);
}

}  // namespace xff::cli
