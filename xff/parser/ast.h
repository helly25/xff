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

#include "xff/regex/regex.h"
#include "xff/registry/descriptor.h"

namespace xff::regex {
class Matcher;  // forward-declared; Expr holds a shared_ptr to a pre-compiled one (pointer only, no dep)
}  // namespace xff::regex

namespace xff::fields {
class Template;  // forward-declared; Expr holds a shared_ptr to -grep=FORMAT's compiled template
}  // namespace xff::fields

namespace xff::parser {

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

// A node in the parsed find expression tree (design.md "CLI grammar & parser").
// Predicates (tests and actions) are leaves; the operators `!`/`-a`/`-o`/`,` and
// the xff extensions `-xor`/`-nand`/`-nor`/`-xnor` are interior nodes. `( )`
// grouping is reflected in tree shape, not as a node.
struct Expr {
  // kNand/kNor/kXnor are the negations of kAnd/kOr/kXor; kXor/kXnor must evaluate
  // both operands (the result depends on both), while kNand/kNor short-circuit like
  // their positive base. See the evaluator.
  enum class Kind { kPredicate, kNot, kAnd, kOr, kXor, kNand, kNor, kXnor, kComma };

  Kind kind;
  // kPredicate: the matched descriptor and its consumed arguments.
  const registry::Descriptor* descriptor = nullptr;
  std::vector<std::string> args;
  // -exec terminated by `+` (batch form): the matched paths are accumulated and
  // the command runs at end-of-walk in ARG_MAX-bounded chunks, not per entry.
  bool exec_batch = false;
  // The node's regex, compiled once at parse time (so evaluation is a lock-free
  // read, not a per-entry compile): -regex/-iregex's pattern (args[0], case folded
  // for -iregex), or -capture/-capturedir's optional extraction regex (args[1]).
  // Null when the node has no regex or the pattern did not compile (-> no match).
  std::shared_ptr<const regex::Matcher> matcher;
  // -grep=FORMAT: the attached output template, compiled once at parse time. Null
  // for a bare -grep (which uses the default path:line:text) and every other node.
  std::shared_ptr<const fields::Template> grep_template;
  // -diff=STYLE: the attached output-style token (u[N]/c[N]/n/y[W]/none); empty for a
  // bare -diff (which defaults to u3) and every other node. Validated in the evaluator.
  std::string diff_style;
  // -hash=ALGO[/ENCODING]: the attached digest spec (e.g. sha256, md5, sha256/base64);
  // empty for a bare -hash (which uses the --hash-algorithm / --hash-encoding defaults) and
  // every other node. Validated before the walk (engine::ValidateHashArgs).
  std::string hash_spec;
  // Case folding forced on by the resolved --case mode (parser::ApplyCaseMode), for the
  // otherwise case-sensitive matchers (-name/-path/-content and, via a recompiled
  // `matcher`, -regex/-rxc/-grep): true under --case=insensitive, or --case=smart when the
  // pattern has no uppercase. Independent of the descriptor's fold_case (the -i variants,
  // always folded) and ctx.fold_name_case (per-entry FS-native folding); the evaluator ORs
  // all three.
  bool case_fold = false;
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
  // The regex grammar for every matcher in this command, resolved once from --regextype at parse
  // time (default RE2). The pattern predicates (-regex/-rxc/-grep + the -capture extraction regex)
  // compile with it; ApplyCaseMode's recompile reuses it.
  regex::Grammar grammar = regex::Grammar::kRe2;
};

}  // namespace xff::parser

#endif  // XFF_PARSER_AST_H_
