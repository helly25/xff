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

#include "xff/parser/parser.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "mbo/status/status_macros.h"
#include "xff/parser/ast.h"
#include "xff/regex/regex.h"
#include "xff/registry/descriptor.h"
#include "xff/registry/registry.h"

namespace xff::parser {
namespace {

// A token begins the find expression if it is a single-dash word, or one of
// the operator/grouping tokens.
bool StartsExpression(std::string_view arg) {
  if (arg.empty()) {
    return false;
  }
  if (arg[0] == '-') {
    return true;
  }
  return arg == "(" || arg == ")" || arg == "!" || arg == ",";
}

bool IsOr(std::string_view t) {
  return t == "-o" || t == "-or";
}

bool IsAnd(std::string_view t) {
  return t == "-a" || t == "-and";
}

bool IsNot(std::string_view t) {
  return t == "!" || t == "-not";
}

// xff extensions: -nand binds at the AND tier, -nor at the OR tier, and -xor /
// -xnor form a new tier between them (NOT > AND > XOR > OR), matching the
// conventional boolean / C bitwise (& ^ |) precedence.
bool IsNand(std::string_view t) {
  return t == "-nand";
}

// Operators at the OR tier (the lowest binary tier): -o / -or / -nor.
bool IsOrTier(std::string_view t) {
  return IsOr(t) || t == "-nor";
}

// Operators at the XOR tier (between AND and OR): -xor / -xnor.
bool IsXorTier(std::string_view t) {
  return t == "-xor" || t == "-xnor";
}

// Pre-compiles a node's regex at parse time so evaluation reads it lock-free:
// -regex/-iregex match args[0]; -capture/-capturedir (Binding::kLabelRegex) carry
// an optional extraction regex in args[1]. Case sensitivity comes from the
// descriptor's fold_case (so -iregex is data, not a name check). Returns null when
// the node carries no regex, or the pattern does not compile (then no-match).
std::shared_ptr<const regex::Matcher> CompileNodeRegex(
    const registry::Descriptor& descriptor,
    const std::vector<std::string>& args) {
  std::string_view pattern;
  if ((descriptor.name == "-regex" || descriptor.name == "-iregex") && !args.empty()) {
    pattern = args[0];
  } else if (descriptor.binding == registry::Binding::kLabelRegex && args.size() > 1 && !args[1].empty()) {
    pattern = args[1];  // the optional =NAME=REGEX extraction regex
  } else {
    return nullptr;
  }
  absl::StatusOr<regex::Matcher> matcher = regex::Matcher::Compile(pattern, descriptor.fold_case);
  if (!matcher.ok()) {
    return nullptr;
  }
  return std::make_shared<const regex::Matcher>(*std::move(matcher));
}

ExprPtr MakePredicate(const registry::Descriptor* descriptor, std::vector<std::string> args) {
  auto expr = std::make_unique<Expr>();
  expr->kind = Expr::Kind::kPredicate;
  expr->descriptor = descriptor;
  expr->matcher = CompileNodeRegex(*descriptor, args);  // compile once, here; eval just reads it
  expr->args = std::move(args);
  return expr;
}

ExprPtr MakeNot(ExprPtr operand) {
  auto expr = std::make_unique<Expr>();
  expr->kind = Expr::Kind::kNot;
  expr->lhs = std::move(operand);
  return expr;
}

ExprPtr MakeBinary(Expr::Kind kind, ExprPtr lhs, ExprPtr rhs) {
  auto expr = std::make_unique<Expr>();
  expr->kind = kind;
  expr->lhs = std::move(lhs);
  expr->rhs = std::move(rhs);
  return expr;
}

// Recursive-descent parser over the find expression tokens. Errors are
// accumulated in `status_`; node-returning methods return nullptr once failed.
//   list := or ( ',' or )*                          // comma: lowest precedence
//   or   := xor ( ('-o'|'-or'|'-nor') xor )*
//   xor  := and ( ('-xor'|'-xnor') and )*           // xff tier, between or and and
//   and  := unary ( ('-a'|'-and'|'-nand')? unary )* // implicit -a; -nand explicit
//   unary:= ('!'|'-not') unary | primary
//   prim := '(' list ')' | PREDICATE arg{arity}
class ExprParser {
 public:
  explicit ExprParser(const std::vector<std::string>& tokens) : tokens_(tokens) {}

  absl::StatusOr<ExprPtr> Parse() {
    ExprPtr expr = ParseComma();
    if (!status_.ok()) {
      return status_;
    }
    if (pos_ != tokens_.size()) {
      return absl::InvalidArgumentError(absl::StrCat("unexpected token: '", tokens_[pos_], "'"));
    }
    return expr;
  }

 private:
  bool AtEnd() const { return pos_ >= tokens_.size(); }

  const std::string& Peek() const { return tokens_[pos_]; }

  void Fail(std::string message) {
    if (status_.ok()) {
      status_ = absl::InvalidArgumentError(std::move(message));
    }
  }

  // Comma has the lowest precedence: a list of OR-expressions. Both sides are
  // always evaluated; the list's value is the right operand's (handled in eval).
  ExprPtr ParseComma() {
    ExprPtr lhs = ParseOr();
    while (status_.ok() && !AtEnd() && Peek() == ",") {
      ++pos_;
      ExprPtr rhs = ParseOr();
      lhs = MakeBinary(Expr::Kind::kComma, std::move(lhs), std::move(rhs));
    }
    return lhs;
  }

  ExprPtr ParseOr() {
    ExprPtr lhs = ParseXor();
    while (status_.ok() && !AtEnd() && IsOrTier(Peek())) {
      const Expr::Kind kind = Peek() == "-nor" ? Expr::Kind::kNor : Expr::Kind::kOr;
      ++pos_;
      ExprPtr rhs = ParseXor();
      lhs = MakeBinary(kind, std::move(lhs), std::move(rhs));
    }
    return lhs;
  }

  // The XOR tier sits between OR and AND (NOT > AND > XOR > OR). -xor / -xnor
  // (xff) bind here; both operands always evaluate (the result needs both).
  ExprPtr ParseXor() {
    ExprPtr lhs = ParseAnd();
    while (status_.ok() && !AtEnd() && IsXorTier(Peek())) {
      const Expr::Kind kind = Peek() == "-xnor" ? Expr::Kind::kXnor : Expr::Kind::kXor;
      ++pos_;
      ExprPtr rhs = ParseAnd();
      lhs = MakeBinary(kind, std::move(lhs), std::move(rhs));
    }
    return lhs;
  }

  ExprPtr ParseAnd() {
    ExprPtr lhs = ParseUnary();
    while (status_.ok() && !AtEnd() && !IsOrTier(Peek()) && !IsXorTier(Peek()) && Peek() != ")" && Peek() != ",") {
      Expr::Kind kind = Expr::Kind::kAnd;
      if (IsNand(Peek())) {
        ++pos_;  // explicit -nand (xff)
        kind = Expr::Kind::kNand;
      } else if (IsAnd(Peek())) {
        ++pos_;  // optional explicit -a
      }
      ExprPtr rhs = ParseUnary();
      lhs = MakeBinary(kind, std::move(lhs), std::move(rhs));
    }
    return lhs;
  }

  ExprPtr ParseUnary() {
    if (!AtEnd() && IsNot(Peek())) {
      ++pos_;
      return MakeNot(ParseUnary());
    }
    return ParsePrimary();
  }

  ExprPtr ParsePrimary() {
    if (AtEnd()) {
      Fail("expected a predicate or '('");
      return nullptr;
    }
    const std::string& token = Peek();
    if (token == "(") {
      ++pos_;
      ExprPtr inner = ParseComma();
      if (!status_.ok()) {
        return nullptr;
      }
      if (AtEnd() || Peek() != ")") {
        Fail("missing ')'");
        return nullptr;
      }
      ++pos_;
      return inner;
    }
    // A primary that declares Binding::kLabelRegex (-capture/-capturedir) carries an
    // attached =NAME[=REGEX] on its own token, then collects a command like -exec:
    // args = [NAME, REGEX (may be empty), cmd...]. The grammar is read from the
    // registry, not a hardcoded name list.
    if (const std::string::size_type eq = token.find('='); eq != std::string::npos) {
      const std::string base = token.substr(0, eq);
      if (const registry::Descriptor* const descriptor = registry::Lookup(base);
          descriptor != nullptr && descriptor->binding == registry::Binding::kLabelRegex) {
        const std::string spec = token.substr(eq + 1);  // NAME[=REGEX]
        const std::string::size_type spec_eq = spec.find('=');
        std::string name = spec_eq == std::string::npos ? spec : spec.substr(0, spec_eq);
        std::string regex = spec_eq == std::string::npos ? std::string() : spec.substr(spec_eq + 1);
        if (name.empty()) {
          Fail(absl::StrCat("'", base, "=' needs a NAME"));
          return nullptr;
        }
        ++pos_;
        std::vector<std::string> command;
        while (!AtEnd() && Peek() != ";") {
          command.push_back(tokens_[pos_++]);
        }
        if (AtEnd()) {
          Fail(absl::StrCat("'", base, "' is missing a ';' terminator"));
          return nullptr;
        }
        if (command.empty()) {
          Fail(absl::StrCat("'", base, "' needs a command before ';'"));
          return nullptr;
        }
        ++pos_;  // consume ';'
        std::vector<std::string> args;
        args.reserve(command.size() + 2);
        args.push_back(std::move(name));
        args.push_back(std::move(regex));
        for (std::string& cmd_token : command) {
          args.push_back(std::move(cmd_token));
        }
        return MakePredicate(descriptor, std::move(args));
      }
    }
    const registry::Descriptor* descriptor = registry::Lookup(token);
    if (descriptor == nullptr) {
      Fail(absl::StrCat("unknown predicate: '", token, "'"));
      return nullptr;
    }
    // A binding primary used bare (no =NAME), e.g. "-capture": the registry binding
    // lets us reject it instead of silently accepting a nameless action.
    if (descriptor->binding == registry::Binding::kLabelRegex) {
      Fail(absl::StrCat("'", token, "' needs a =NAME (e.g. ", token, "=NAME)"));
      return nullptr;
    }
    if (descriptor->kind == registry::Kind::kOperator) {
      Fail(absl::StrCat("unexpected operator: '", token, "'"));
      return nullptr;
    }
    ++pos_;
    // -exec/-execdir take a variable-length command terminated by ';'.
    if (descriptor->arity < 0) {
      std::vector<std::string> command;
      while (!AtEnd() && Peek() != ";" && Peek() != "+") {
        command.push_back(tokens_[pos_++]);
      }
      if (AtEnd()) {
        Fail(absl::StrCat("'", token, "' is missing a ';' or '+' terminator"));
        return nullptr;
      }
      const bool batch = Peek() == "+";
      if (command.empty()) {
        Fail(absl::StrCat("'", token, "' needs a command before '", Peek(), "'"));
        return nullptr;
      }
      if (batch && descriptor->name != "-exec" && descriptor->name != "-execdir") {
        // Only -exec/-execdir batch; the interactive -ok/-okdir never take '+'.
        Fail(absl::StrCat("'", token, " ... +' is not supported; use ';'"));
        return nullptr;
      }
      if (batch && command.back() != "{}") {
        Fail(absl::StrCat("'", token, " ... +' requires '{}' as the last argument before '+'"));
        return nullptr;
      }
      ++pos_;  // consume ';' or '+'
      ExprPtr node = MakePredicate(descriptor, std::move(command));
      if (node != nullptr) {
        node->exec_batch = batch;
      }
      return node;
    }
    std::vector<std::string> args;
    for (int i = 0; i < descriptor->arity; ++i) {
      if (AtEnd()) {
        Fail(absl::StrCat("predicate '", token, "' is missing an argument"));
        return nullptr;
      }
      args.push_back(tokens_[pos_++]);
    }
    return MakePredicate(descriptor, std::move(args));
  }

  const std::vector<std::string>& tokens_;
  std::size_t pos_ = 0;
  absl::Status status_ = absl::OkStatus();
};

// Returns the first expression primary (pre-order, left to right in evaluation
// order) whose descriptor is tagged as an xff extension, or nullptr if the tree
// has none. The strict find-style check uses it to name the offending primary.
const registry::Descriptor* FirstXffExtension(const Expr* expr) {
  if (expr == nullptr) {
    return nullptr;
  }
  if (expr->kind == Expr::Kind::kPredicate) {
    const registry::Descriptor* const descriptor = expr->descriptor;
    return descriptor != nullptr && descriptor->style == registry::Style::kXff ? descriptor : nullptr;
  }
  if (const registry::Descriptor* const found = FirstXffExtension(expr->lhs.get()); found != nullptr) {
    return found;
  }
  return FirstXffExtension(expr->rhs.get());
}

// Returns the first day-time predicate (-mtime/-atime/-ctime, pre-order) whose
// argument is the xff duration form -- a space-bearing span like "-3 weeks 3
// hours" -- or nullptr. The bare day count and the BSD unit suffix (-1h) never
// contain a space, so they stay find-compatible; only the word/compound span is
// an xff extension the strict find style rejects. See docs/design-find-flavors.md.
const Expr* FirstXffDurationValue(const Expr* expr) {
  if (expr == nullptr) {
    return nullptr;
  }
  if (expr->kind == Expr::Kind::kPredicate) {
    const registry::Descriptor* const descriptor = expr->descriptor;
    if (descriptor == nullptr) {
      return nullptr;
    }
    const std::string_view name = descriptor->name;
    const bool day_time = name == "-mtime" || name == "-atime" || name == "-ctime";
    return day_time && !expr->args.empty() && expr->args.front().find(' ') != std::string::npos ? expr : nullptr;
  }
  if (const Expr* const found = FirstXffDurationValue(expr->lhs.get()); found != nullptr) {
    return found;
  }
  return FirstXffDurationValue(expr->rhs.get());
}

// Returns the canonical name of the first xff-only logical operator node
// (-xor/-nand/-nor/-xnor, pre-order), or empty if none. These are interior nodes
// with no descriptor, so FirstXffExtension (which inspects predicate descriptors)
// cannot see them; the strict find style rejects them through this instead.
std::string_view FirstXffOperator(const Expr* expr) {
  if (expr == nullptr) {
    return {};
  }
  switch (expr->kind) {
    case Expr::Kind::kNand: return "-nand";
    case Expr::Kind::kNor: return "-nor";
    case Expr::Kind::kXnor: return "-xnor";
    case Expr::Kind::kXor: return "-xor";
    default: break;
  }
  if (const std::string_view found = FirstXffOperator(expr->lhs.get()); !found.empty()) {
    return found;
  }
  return FirstXffOperator(expr->rhs.get());
}

}  // namespace

absl::StatusOr<Command> Parse(const std::vector<std::string>& args) {
  Command cmd;
  std::size_t i = 0;

  // Globals: leading '-'/'+' tokens before the first root; '--' ends them.
  for (; i < args.size(); ++i) {
    const std::string& arg = args[i];
    if (arg == "--") {
      ++i;
      break;
    }
    if (!arg.empty() && (arg[0] == '-' || arg[0] == '+')) {
      cmd.globals.push_back(arg);
    } else {
      break;
    }
  }

  // Roots: operands until the expression begins.
  for (; i < args.size(); ++i) {
    if (StartsExpression(args[i])) {
      break;
    }
    cmd.roots.push_back(args[i]);
  }

  // Expression: the remaining tokens, parsed to a tree.
  const std::vector<std::string> expr_tokens(args.begin() + static_cast<std::ptrdiff_t>(i), args.end());
  if (!expr_tokens.empty()) {
    ExprParser parser(expr_tokens);
    MBO_ASSIGN_OR_RETURN(cmd.expression, parser.Parse());
  }
  return cmd;
}

absl::Status EnforceStyle(const Command& command, registry::Style style) {
  if (style != registry::Style::kFind) {
    return absl::OkStatus();  // the xff style accepts the full vocabulary
  }
  if (const registry::Descriptor* const ext = FirstXffExtension(command.expression.get()); ext != nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat(
            "'", ext->name,
            "' is an xff extension, not available under the find style (--config=find); use --config=xff"));
  }
  if (const std::string_view op = FirstXffOperator(command.expression.get()); !op.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat(
            "'", op, "' is an xff extension, not available under the find style (--config=find); use --config=xff"));
  }
  if (const Expr* const dur = FirstXffDurationValue(command.expression.get()); dur != nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat(
            "the duration form of '", dur->descriptor->name,
            "' (e.g. \"-3 weeks 3 hours\") is an xff extension, not available under the find style (--config=find); "
            "use a day count, a unit suffix like -1h, or --config=xff"));
  }
  return absl::OkStatus();
}

}  // namespace xff::parser
