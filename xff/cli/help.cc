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

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "xff/cli/globals.h"
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

std::string RenderGlobalFlag(const GlobalFlag& flag) {
  return absl::StrCat(flag.display, "  (global, ", flag.xff ? "xff" : "find", ")\n    ", flag.summary, "\n");
}

std::string RenderIndex() {
  std::string out =
      "xff vocabulary. Use `--help=NAME` for one entry (e.g. `--help=-regex`, `--help=--sort`), "
      "or `--help` for the usage overview.\n";

  // Whole-run global options, grouped as the usage page groups them (array order).
  std::string_view group;
  for (const GlobalFlag& flag : Globals()) {
    if (flag.group != group) {
      group = flag.group;
      absl::StrAppend(&out, "\n", group, ":\n");
    }
    absl::StrAppendFormat(&out, "  %-30s%s\n", flag.display, flag.summary);
  }

  // Expression vocabulary, grouped by kind.
  struct Section {
    registry::Kind kind;
    std::string_view title;
  };

  for (const Section& section :
       {Section{registry::Kind::kTest, "Tests"}, Section{registry::Kind::kAction, "Actions"},
        Section{registry::Kind::kOperator, "Operators"}}) {
    absl::StrAppend(&out, "\n", section.title, ":\n");
    for (const registry::Descriptor& descriptor : registry::All()) {
      if (descriptor.kind == section.kind) {
        absl::StrAppendFormat(&out, "  %-24s%s\n", descriptor.name, descriptor.summary);
      }
    }
  }
  return out;
}

}  // namespace

absl::StatusOr<std::string> RenderHelp(std::string_view topic) {
  if (topic.empty() || topic == "list" || topic == "all") {
    return RenderIndex();
  }
  // Expression primary / operator / action (leading-dash convenience: `--help=regex`).
  const registry::Descriptor* descriptor = registry::Lookup(topic);
  if (descriptor == nullptr && topic.front() != '-' && topic.front() != '!') {
    descriptor = registry::Lookup(absl::StrCat("-", topic));
  }
  if (descriptor != nullptr) {
    return RenderOne(*descriptor);
  }
  // Whole-run global option (leading-dashes convenience: `--help=sort`).
  const GlobalFlag* global = LookupGlobal(topic);
  if (global == nullptr && topic.front() != '-') {
    global = LookupGlobal(absl::StrCat("--", topic));
  }
  if (global != nullptr) {
    return RenderGlobalFlag(*global);
  }
  return absl::NotFoundError("");  // the caller holds the topic and composes the message
}

}  // namespace xff::cli
