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

#ifndef XFF_CLI_HELP_H_
#define XFF_CLI_HELP_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "xff/registry/descriptor.h"

namespace xff::cli {

// Renders {code, description} rows as an aligned indented list:
// `<indent><code padded to the widest code + 2 spaces><description>`. The shared layout
// for the sub-vocabulary topics (--help=printf / --help=time / --help=size) so every
// code list indents and aligns the same way.
std::string RenderDocRows(
    std::string_view indent,
    const std::vector<std::pair<std::string_view, std::string_view>>& rows);

// The argument-shape hint shown after a primary's name: " ARG" (arity 1), " ARG ARG"
// (arity 2), " CMD... ;" (variadic), "=NAME[=REGEX] CMD... ;" (a binding action), or
// "" (a flag-like primary). Read from the descriptor grammar so `--help` and the man
// page render the synopsis identically and neither drifts from the parser.
std::string ArgHint(const registry::Descriptor& descriptor);

// One `--help=TOPIC` topic, for the generated topic index. The single source of the
// help-system map: the usage page's Help: section, `--help=list`, and `--help=help`
// all render HelpTopics() through RenderTopicIndex(), so the advertised topic list can
// never drift from what RenderHelp actually accepts (help_render_test enforces it).
struct HelpTopic {
  std::string_view name;                  // the topic keyword (--help=NAME)
  std::vector<std::string_view> aliases;  // alternate spellings, or empty
  std::string_view summary;               // one-line description
};

// The meta-topics of the help system (help, list, expressions, fields, styles, full).
// Not the per-entry NAME lookup, which is the registry/global fallback in RenderHelp.
std::vector<HelpTopic> HelpTopics();

// Formats HelpTopics() as a block of `<indent>name   summary (also: aliases)` lines,
// the name column padded to `name_width`. Shared by the usage page (where name_width is
// chosen so the summaries line up with the flag summaries), `--help=list`, and
// `--help=help`.
std::string RenderTopicIndex(std::string_view indent, std::size_t name_width = 13);

// Renders the whole-run options grouped by GlobalFlag.group, one "display  summary"
// line per flag, at `group_indent` (flags indented two spaces more). Generated from
// Globals() so the usage page (--help) and the index (--help=list) share one source
// and neither hand-maintains flag descriptions.
std::string RenderOptions(std::string_view group_indent);

// One meta / doc flag for the usage page's Help: section (-h/--help, --help=NAME,
// --help=TOPIC, --help-full, --man, --markdown, --version). These are consumed before
// parsing (not in Globals(), never looked up), so they carry their own doc SOT here
// instead of being hand-written; RenderHelpSection() renders them + the topic index.
struct HelpFlag {
  std::string_view display;  // e.g. "-h, --help, -help", "--man"
  std::string_view summary;  // one-line description
};

std::vector<HelpFlag> HelpFlags();

// The usage page's "Help:" section: the HelpFlags() rows plus the --help=TOPIC index
// (from HelpTopics()), so the CLI hand-maintains no help-flag text.
std::string RenderHelpSection();

// Renders the `--help=TOPIC` help from the registry (the single source of truth, so
// help can never drift from the parser's vocabulary). An empty topic (or "list")
// returns the index of the whole vocabulary grouped by kind; "help" the help-system
// guide; "expressions" the primary vocabulary; "fields"/"format" the {field}
// vocabulary; "full"/"long"/"all" the full detailed reference. A named topic (e.g.
// "-regex", "-xor", or the dash-less "regex") returns that entry's detailed help. An
// unknown topic is a plain `NotFoundError` (the status code is the signal; the caller
// holds the topic and composes the user-facing message). `styles`/`flavors` are
// rendered by the CLI (they need the engine), not here.
absl::StatusOr<std::string> RenderHelp(std::string_view topic);

}  // namespace xff::cli

#endif  // XFF_CLI_HELP_H_
