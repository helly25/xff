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

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
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

// find treats -maxdepth/-mindepth/-depth/-xdev as global positional options
// (they apply regardless of where they sit in the expression); collect them into
// the walk limits. Last occurrence wins, as in find.
void ScanDepthOptions(const parser::Expr& expr, WalkOptions* options) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: {
      if (expr.descriptor->name == "-depth") {
        options->post_order = true;
        break;
      }
      if (expr.descriptor->name == "-xdev") {
        options->single_filesystem = true;
        break;
      }
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
    case parser::Expr::Kind::kComma:
      ScanDepthOptions(*expr.lhs, options);
      ScanDepthOptions(*expr.rhs, options);
      break;
  }
}

// find's -H/-L/-P select symlink handling; they are leading global options and
// the last one wins (default -P). The parser collects them in command.globals.
SymlinkMode ResolveSymlinkMode(const std::vector<std::string>& globals) {
  SymlinkMode mode = SymlinkMode::kNever;
  for (const std::string& global : globals) {
    if (global == "-P") {
      mode = SymlinkMode::kNever;
    } else if (global == "-L") {
      mode = SymlinkMode::kAll;
    } else if (global == "-H") {
      mode = SymlinkMode::kRoots;
    }
  }
  return mode;
}

}  // namespace

int RunFind(const parser::Command& command, const vfs::FileSystem& fs, EmitFn emit, WalkErrorFn on_error) {
  const parser::Expr* const expression = command.expression.get();
  const bool has_action = expression != nullptr && ContainsAction(*expression);
  WalkOptions options;
  options.symlinks = ResolveSymlinkMode(command.globals);
  if (expression != nullptr) {
    ScanDepthOptions(*expression, &options);
  }
  // Capture one reference instant so every entry's age test (-mtime/-mmin) is
  // measured against the same clock, matching find's start-time semantics.
  const absl::Time now = absl::Now();
  int errors = 0;

  const absl::Status status = Walk(
      fs, command.roots, options,
      [&](const Visit& visit) {
        Control control;
        const bool matched = expression == nullptr || Evaluate(*expression, visit, emit, fs, now, control);
        if (matched && !has_action) {
          emit(absl::StrCat(visit.path, "\n"));  // implicit -print
        }
        if (control.quit) return WalkAction::kStop;
        if (control.prune) return WalkAction::kPrune;
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
