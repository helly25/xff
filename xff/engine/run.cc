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

#include "xff/engine/run.h"

#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/walk.h"
#include "xff/parser/ast.h"
#include "xff/registry/descriptor.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

// Parses a non-negative decimal integer (find depth arguments).
bool ParseNonNegInt(std::string_view text, int* out) {
  if (text.empty()) {
    return false;
  }
  int value = 0;
  for (const char digit : text) {
    if (digit < '0' || digit > '9') {
      return false;
    }
    value = value * 10 + (digit - '0');
  }
  *out = value;
  return true;
}

// find treats -maxdepth/-mindepth as global positional options (they apply
// regardless of where they sit in the expression); collect them into the walk
// limits. Last occurrence wins, as in find.
void ScanDepthOptions(const parser::Expr& expr, WalkOptions* options) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: {
      int value = 0;
      if (!expr.args.empty() && ParseNonNegInt(expr.args.front(), &value)) {
        if (expr.descriptor->name == "-maxdepth") {
          options->max_depth = value;
        } else if (expr.descriptor->name == "-mindepth") {
          options->min_depth = value;
        }
      }
      break;
    }
    case parser::Expr::Kind::kNot:
      ScanDepthOptions(*expr.lhs, options);
      break;
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
      ScanDepthOptions(*expr.lhs, options);
      ScanDepthOptions(*expr.rhs, options);
      break;
  }
}

}  // namespace

int RunFind(const parser::Command& command, const vfs::FileSystem& fs, EmitFn emit, WalkErrorFn on_error) {
  const parser::Expr* const expression = command.expression.get();
  const bool has_action = expression != nullptr && ContainsAction(*expression);
  WalkOptions options;
  if (expression != nullptr) {
    ScanDepthOptions(*expression, &options);
  }
  int errors = 0;

  const absl::Status status = Walk(
      fs, command.roots, options,
      [&](const Visit& visit) {
        const bool matched = expression == nullptr || Evaluate(*expression, visit, emit, fs);
        if (matched && !has_action) {
          emit(absl::StrCat(visit.path, "\n"));  // implicit -print
        }
        return WalkAction::kContinue;
      },
      [&](std::string_view path, const absl::Status& error_status) {
        ++errors;
        on_error(path, error_status);
      });
  if (!status.ok()) {
    ++errors;  // Fatal traversal error (none today; per-path errors handled above).
  }

  return errors;
}

}  // namespace xff::engine
