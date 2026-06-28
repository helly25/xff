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

#include "xff/cli/markdown.h"

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

// One Markdown bullet: the name/flag in backticks (so its =NAME / [..] / | are
// literal), the summary as prose, and an italic flavor tag for xff-only items.
void Bullet(std::string* out, std::string_view code, std::string_view summary, bool xff) {
  absl::StrAppendFormat(out, "- `%s` - %s%s\n", code, summary, xff ? " _(xff)_" : "");
}

}  // namespace

std::string MarkdownReference() {
  std::string out =
      "# xff\n\n"
      "eXtended File Find: a find(1)-compatible file finder with modern extensions. "
      "Items marked _(xff)_ are extensions over find.\n\n"
      "**Usage:** `xff [option...] [path...] [expression]`\n\n"
      "## Options\n";

  std::string_view group;
  for (const GlobalFlag& flag : Globals()) {
    if (flag.group != group) {
      group = flag.group;
      absl::StrAppendFormat(&out, "\n### %s\n\n", group);
    }
    Bullet(&out, flag.display, flag.summary, flag.xff);
  }

  absl::StrAppend(&out, "\n## Expression\n");

  struct Section {
    registry::Kind kind;
    std::string_view title;
  };

  for (const Section& section :
       {Section{registry::Kind::kTest, "Tests"}, Section{registry::Kind::kAction, "Actions"},
        Section{registry::Kind::kOperator, "Operators"}}) {
    absl::StrAppendFormat(&out, "\n### %s\n\n", section.title);
    for (const registry::Descriptor& descriptor : registry::All()) {
      if (descriptor.kind == section.kind) {
        Bullet(
            &out, absl::StrCat(descriptor.name, ArgHint(descriptor)), descriptor.summary,
            descriptor.style == registry::Style::kXff);
      }
    }
  }
  return out;
}

}  // namespace xff::cli
