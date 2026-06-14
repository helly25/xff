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

// FNM_CASEFOLD and POSIX fnmatch() are hidden by glibc under the strict
// `-std=c++23` we build with; request them explicitly. No effect on macOS.
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#include "xff/engine/evaluate.h"

#include <fnmatch.h>

#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "xff/engine/walk.h"
#include "xff/parser/ast.h"
#include "xff/registry/descriptor.h"
#include "xff/vfs/entry.h"

namespace xff::engine {
namespace {

bool Fnmatch(std::string_view pattern, std::string_view text, int flags) {
  return ::fnmatch(std::string(pattern).c_str(), std::string(text).c_str(), flags) == 0;
}

bool MatchesType(std::string_view arg, vfs::FileType type) {
  if (arg.size() != 1) {
    return false;  // Multi-type lists ("-type f,d") are a GNU extension; deferred.
  }
  switch (arg.front()) {
    case 'f': return type == vfs::FileType::kRegular;
    case 'd': return type == vfs::FileType::kDirectory;
    case 'l': return type == vfs::FileType::kSymlink;
    case 'b': return type == vfs::FileType::kBlockDevice;
    case 'c': return type == vfs::FileType::kCharDevice;
    case 'p': return type == vfs::FileType::kFifo;
    case 's': return type == vfs::FileType::kSocket;
    default: return false;
  }
}

bool EvaluatePredicate(const parser::Expr& expr, const Visit& visit, EmitFn emit) {
  const std::string_view name = expr.descriptor->name;
  const bool has_arg = !expr.args.empty();
  if (name == "-true") return true;
  if (name == "-false") return false;
  if (name == "-name") return has_arg && Fnmatch(expr.args.front(), visit.name, 0);
  if (name == "-iname") return has_arg && Fnmatch(expr.args.front(), visit.name, FNM_CASEFOLD);
  if (name == "-path") return has_arg && Fnmatch(expr.args.front(), visit.path, 0);
  if (name == "-ipath") return has_arg && Fnmatch(expr.args.front(), visit.path, FNM_CASEFOLD);
  if (name == "-type") return has_arg && MatchesType(expr.args.front(), visit.metadata.type);
  if (name == "-print") {
    emit(absl::StrCat(visit.path, "\n"));
    return true;
  }
  if (name == "-print0") {
    std::string record(visit.path);
    record.push_back('\0');
    emit(record);
    return true;
  }
  // Predicates not yet implemented evaluate to true (no-op); wired in follow-ups.
  return true;
}

}  // namespace

bool Evaluate(const parser::Expr& expr, const Visit& visit, EmitFn emit) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: return EvaluatePredicate(expr, visit, emit);
    case parser::Expr::Kind::kNot: return !Evaluate(*expr.lhs, visit, emit);
    case parser::Expr::Kind::kAnd: return Evaluate(*expr.lhs, visit, emit) && Evaluate(*expr.rhs, visit, emit);
    case parser::Expr::Kind::kOr: return Evaluate(*expr.lhs, visit, emit) || Evaluate(*expr.rhs, visit, emit);
  }
  return true;  // Unreachable: every Expr::Kind returns above.
}

bool ContainsAction(const parser::Expr& expr) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: return expr.descriptor->kind == registry::Kind::kAction;
    case parser::Expr::Kind::kNot: return ContainsAction(*expr.lhs);
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr: return ContainsAction(*expr.lhs) || ContainsAction(*expr.rhs);
  }
  return false;  // Unreachable: every Expr::Kind returns above.
}

}  // namespace xff::engine
