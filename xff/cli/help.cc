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

#include "xff/cli/help.h"

#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "xff/registry/descriptor.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

std::string_view KindLabel(registry::Kind kind) {
  switch (kind) {
    case registry::Kind::kAction: return "action";
    case registry::Kind::kOperator: return "operator";
    case registry::Kind::kTest: return "test";
  }
  return "";
}

// The argument shape shown after the name, read from the descriptor grammar so it
// stays in lockstep with the parser (arity / binding), not hand-maintained.
std::string ArgHint(const registry::Descriptor& descriptor) {
  if (descriptor.binding == registry::Binding::kLabelRegex) {
    return "=NAME[=REGEX] CMD... ;";
  }
  if (descriptor.arity < 0) {
    return " CMD... ;";  // variadic until ';' (or '+' for -exec / -execdir)
  }
  std::string hint;
  for (int i = 0; i < descriptor.arity; ++i) {
    absl::StrAppend(&hint, " ARG");
  }
  return hint;
}

// "(test, find)" / "(action, xff, runs commands)".
std::string Tags(const registry::Descriptor& descriptor) {
  std::vector<std::string_view> tags;
  tags.push_back(KindLabel(descriptor.kind));
  tags.push_back(descriptor.style == registry::Style::kXff ? "xff" : "find");
  if (descriptor.safety == registry::Safety::kSecurity) {
    tags.emplace_back("runs commands");
  } else if (descriptor.safety == registry::Safety::kSafety) {
    tags.emplace_back("modifies the filesystem");
  }
  return absl::StrCat("(", absl::StrJoin(tags, ", "), ")");
}

std::string RenderOne(const registry::Descriptor& descriptor) {
  return absl::StrCat(descriptor.name, ArgHint(descriptor), "  ", Tags(descriptor), "\n    ", descriptor.summary, "\n");
}

std::string RenderIndex() {
  std::string out =
      "xff expression vocabulary. Use `--help=NAME` for one entry (e.g. `--help=-regex`), "
      "or `--help` for the usage overview.\n";

  struct Group {
    registry::Kind kind;
    std::string_view title;
  };

  for (const Group& group :
       {Group{registry::Kind::kTest, "Tests"}, Group{registry::Kind::kAction, "Actions"},
        Group{registry::Kind::kOperator, "Operators"}}) {
    absl::StrAppend(&out, "\n", group.title, ":\n");
    for (const registry::Descriptor& descriptor : registry::All()) {
      if (descriptor.kind == group.kind) {
        absl::StrAppendFormat(&out, "  %-24s%s\n", descriptor.name, descriptor.summary);
      }
    }
  }
  return out;
}

}  // namespace

HelpResult RenderHelp(std::string_view topic) {
  if (topic.empty() || topic == "list" || topic == "all") {
    return {.text = RenderIndex(), .found = true};
  }
  const registry::Descriptor* descriptor = registry::Lookup(topic);
  if (descriptor == nullptr && topic.front() != '-' && topic.front() != '!') {
    descriptor = registry::Lookup(absl::StrCat("-", topic));  // friendly: `xff help regex`
  }
  if (descriptor == nullptr) {
    return {
        .text = absl::StrCat("xff: no help topic '", topic, "'; try `xff help` for the list\n"),
        .found = false,
    };
  }
  return {.text = RenderOne(*descriptor), .found = true};
}

}  // namespace xff::cli
