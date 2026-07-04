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

#include "xff/cli/manpage.h"

#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "xff/cli/globals.h"
#include "xff/cli/help.h"
#include "xff/registry/descriptor.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

// Escape text for a roff source line: backslash starts an escape, and a hyphen is
// rendered as `\-` so option dashes are real hyphen-minus (copy-pasteable), per the
// man-page convention. No content here begins a line with `.`/`'`, the other roff
// control characters, so those need no guarding.
std::string Roff(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    if (c == '\\') {
      out += "\\\\";
    } else if (c == '-') {
      out += "\\-";
    } else {
      out += c;
    }
  }
  return out;
}

// A `.TP` tagged-paragraph entry: a bold header line then the description, with a
// trailing " (xff extension)" when the item is xff-only.
void Entry(std::string* out, std::string_view header, std::string_view summary, bool xff) {
  absl::StrAppendFormat(out, ".TP\n.B %s\n%s%s\n", Roff(header), Roff(summary), xff ? " (xff extension)" : "");
}

}  // namespace

std::string ManPage() {
  std::string out;
  absl::StrAppend(&out, ".TH xff 1 \"\" \"xff\" \"User Commands\"\n");
  absl::StrAppend(
      &out, ".SH NAME\nxff \\- eXtended File Find, a find(1)-compatible file finder with modern extensions\n");
  absl::StrAppend(&out, ".SH SYNOPSIS\n.B xff\n[option...] [path...] [expression]\n");
  absl::StrAppend(
      &out,
      ".SH DESCRIPTION\n"
      "xff walks each starting path and acts on the entries matching an expression, "
      "like find(1). With no path it searches the current directory; with no action it "
      "prints each match.\n"
      ".PP\n"
      "xff has two flavors selected by the program name: invoked as \\fBfind\\fR it is "
      "strict find (only the standard vocabulary); invoked as \\fBxff\\fR it enables the "
      "modern extensions. An explicit \\fB\\-\\-config=find|xff\\fR overrides the program "
      "name. Options marked \"(xff extension)\" below are the additions over find.\n");

  absl::StrAppend(&out, ".SH OPTIONS\n");
  std::string_view group;
  for (const GlobalFlag& flag : Globals()) {
    if (flag.group != group) {
      group = flag.group;
      absl::StrAppendFormat(&out, ".SS %s\n", flag.header);
    }
    Entry(&out, flag.display, flag.summary, flag.xff);
  }

  absl::StrAppend(&out, ".SH EXPRESSION\n");

  struct Section {
    registry::Kind kind;
    std::string_view title;
  };

  for (const Section& section :
       {Section{registry::Kind::kTest, "Tests"}, Section{registry::Kind::kAction, "Actions"},
        Section{registry::Kind::kOperator, "Operators"}}) {
    absl::StrAppendFormat(&out, ".SS %s\n", section.title);
    for (const registry::Descriptor& descriptor : registry::All()) {
      if (descriptor.kind == section.kind) {
        Entry(
            &out, absl::StrCat(descriptor.name, ArgHint(descriptor)), descriptor.summary,
            descriptor.style == registry::Style::kXff);
      }
    }
  }

  absl::StrAppend(
      &out,
      ".SH EXIT STATUS\n"
      "0 on success, 2 on error. With \\fB\\-\\-quiet\\fR or \\fB\\-\\-exit\\-match\\fR the "
      "exit is 0 when something matched and 1 when nothing did (an error still outranks "
      "the match status).\n");
  absl::StrAppend(&out, ".SH SEE ALSO\n.BR find (1)\n");
  return out;
}

}  // namespace xff::cli
