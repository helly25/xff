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

#ifndef XFF_PARSER_AST_H_
#define XFF_PARSER_AST_H_

#include <memory>
#include <string>
#include <vector>

#include "xff/registry/descriptor.h"

namespace xff::regex {
class Matcher;  // forward-declared; Expr holds a shared_ptr to a pre-compiled one (pointer only, no dep)
}  // namespace xff::regex

namespace xff::parser {

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

// A node in the parsed find expression tree (design.md "CLI grammar & parser").
// Predicates (tests and actions) are leaves; `!`/`-a`/`-o`/`,` are interior nodes.
// `( )` grouping is reflected in tree shape, not as a node.
struct Expr {
  enum class Kind { kPredicate, kNot, kAnd, kOr, kComma };

  Kind kind;
  // kPredicate: the matched descriptor and its consumed arguments.
  const registry::Descriptor* descriptor = nullptr;
  std::vector<std::string> args;
  // The node's regex, compiled once at parse time (so evaluation is a lock-free
  // read, not a per-entry compile): -regex/-iregex's pattern (args[0], case folded
  // for -iregex), or -capture/-capturedir's optional extraction regex (args[1]).
  // Null when the node has no regex or the pattern did not compile (-> no match).
  std::shared_ptr<const regex::Matcher> matcher;
  // kNot: operand in `lhs`. kAnd / kOr / kComma: operands in `lhs` and `rhs`.
  ExprPtr lhs;
  ExprPtr rhs;
};

// A parsed command line: order-independent globals, search roots, and the
// position-dependent expression tree (null when no expression was given).
struct Command {
  std::vector<std::string> globals;
  std::vector<std::string> roots;
  ExprPtr expression;
};

}  // namespace xff::parser

#endif  // XFF_PARSER_AST_H_
