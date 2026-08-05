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

#ifndef XFF_CLI_HELP_PARSE_H_
#define XFF_CLI_HELP_PARSE_H_

#include <string_view>

#include "xff/cli/help_model.h"

// Parses authored help text into the help model. The ONLY markup is backticks
// (see docs/design-help-model.md): a single `code` span becomes an inline kCode
// run, and a triple-backtick fence becomes a verbatim Example. No #, -, or *
// markup is recognized - those are exactly xff's flag / glob / regex characters,
// so they stay literal; headings, bullets, refs and the like are typed nodes the
// caller builds, never parsed from strings.
namespace xff::cli {

// Splits one authored string into inline runs. `code` (single backticks) becomes
// a kCode run and the rest kText; a doubled backtick `` is a literal backtick, and
// an unterminated backtick stays literal. No other markup.
[[nodiscard]] Inlines ParseInline(std::string_view text);

// Splits an authored (possibly multi-line) string into block content. A run of
// non-blank lines is one Prose paragraph (its text, whitespace-normalized, run
// through ParseInline); blank lines separate paragraphs; a line opening with ```
// starts a verbatim Example that runs to the closing ``` (the info string after
// the opening fence, e.g. sh, becomes Example.lang).
[[nodiscard]] Blocks ParseBlocks(std::string_view text);

}  // namespace xff::cli

#endif  // XFF_CLI_HELP_PARSE_H_
