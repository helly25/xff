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

#include <string>
#include <string_view>

namespace xff::cli {

// The outcome of an `xff help [topic]` request.
struct HelpResult {
  std::string text;   // the rendered help, always populated
  bool found = true;  // false when `topic` named no known primary / operator / action
};

// Renders the `--help=TOPIC` help from the registry (the single source of truth, so
// help can never drift from the parser's vocabulary). An empty topic (or "list" /
// "all") returns the index of the whole expression vocabulary grouped by kind. A
// named topic (e.g. "-regex", "-xor", or the dash-less "regex") returns that entry's
// detailed help; `found` is false (and the text is a short "no such topic" note)
// when the topic is unknown.
HelpResult RenderHelp(std::string_view topic);

}  // namespace xff::cli

#endif  // XFF_CLI_HELP_H_
