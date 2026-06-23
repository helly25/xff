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

#ifndef XFF_PARSER_PARSER_H_
#define XFF_PARSER_PARSER_H_

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xff/parser/ast.h"
#include "xff/registry/descriptor.h"

namespace xff::parser {

// Parses argv into [globals] [roots] [expression] per the xff grammar:
// leading '-'/'+' global flags up to the first directory or an explicit '--',
// one or more roots, then the position-dependent find expression
// (precedence ! > -a > -o, implicit -a between adjacent predicates, ( )
// grouping). Returns an error for an unknown predicate, a predicate missing
// arguments, an unexpected operator, or unbalanced parentheses.
absl::StatusOr<Command> Parse(const std::vector<std::string>& args);

// Enforces the active find/xff style on a parsed command. Under the strict find
// style (registry::Style::kFind, selected by --config=find), any expression
// primary tagged registry::Style::kXff (an xff extension such as -println or
// -capture) is outside find's vocabulary, so this returns InvalidArgument naming
// the first such primary; the xff style (the default) accepts the full vocabulary
// and always returns Ok. design-config.md "CLI selectors".
absl::Status EnforceStyle(const Command& command, registry::Style style);

}  // namespace xff::parser

#endif  // XFF_PARSER_PARSER_H_
