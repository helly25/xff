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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/walk.h"
#include "xff/fields/fields.h"
#include "xff/parser/ast.h"
#include "xff/registry/descriptor.h"
#include "xff/render/render.h"
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
      if (expr.descriptor->name == "-depth" || expr.descriptor->name == "-delete") {
        options->post_order = true;  // -delete implies -depth
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

// xff's modern output selector (leading globals, last wins, default plain):
// --format=plain|nul|jsonl, with -0 a shorthand for NUL. find's -print/-print0
// keep their fixed formats; this drives only the implicit (default) print.
render::Format ResolveFormat(const std::vector<std::string>& globals) {
  render::Format format = render::Format::kPlain;
  for (const std::string& global : globals) {
    if (global == "-0" || global == "--format=nul") {
      format = render::Format::kNul;
    } else if (global == "--format=plain") {
      format = render::Format::kPlain;
    } else if (global == "--format=jsonl") {
      format = render::Format::kJsonl;
    }
  }
  return format;
}

// --template=TMPL renders each match through the field vocabulary (xff-native),
// overriding --format for the implicit print. Last occurrence wins; nullopt when
// absent.
std::optional<std::string> ResolveTemplate(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--template=";
  std::optional<std::string> tmpl;
  for (const std::string& global : globals) {
    if (global.starts_with(kPrefix)) {
      tmpl = global.substr(kPrefix.size());
    }
  }
  return tmpl;
}

// Wraps a FileSystem so Remove previews (emits the path) instead of deleting:
// backs --dry-run for -delete. ReadDir/Stat pass through unchanged.
class DryRunFileSystem : public vfs::FileSystem {
 public:
  DryRunFileSystem(const vfs::FileSystem& fs, EmitFn preview) : fs_(fs), preview_(preview) {}
  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view dir) const override { return fs_.ReadDir(dir); }
  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool follow) const override {
    return fs_.Stat(path, follow);
  }
  absl::Status Remove(std::string_view path) const override {
    preview_(absl::StrCat(path, "\n"));  // would-delete preview; nothing is removed
    return absl::OkStatus();
  }

 private:
  const vfs::FileSystem& fs_;
  EmitFn preview_;
};

// True if the expression contains an armed (effectful) action -- -delete or
// -exec. --safe refuses these. (-delete additionally implies -depth, applied
// in ScanDepthOptions.)
bool ContainsArmedAction(const parser::Expr& expr) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate:
      return expr.descriptor->name == "-delete" || expr.descriptor->name == "-exec";
    case parser::Expr::Kind::kNot: return ContainsArmedAction(*expr.lhs);
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kComma: return ContainsArmedAction(*expr.lhs) || ContainsArmedAction(*expr.rhs);
  }
  return false;
}

bool HasGlobal(const std::vector<std::string>& globals, std::string_view flag) {
  for (const std::string& global : globals) {
    if (global == flag) {
      return true;
    }
  }
  return false;
}

}  // namespace

int RunFind(const parser::Command& command, const vfs::FileSystem& fs, EmitFn emit, WalkErrorFn on_error) {
  const parser::Expr* const expression = command.expression.get();
  const bool has_action = expression != nullptr && ContainsAction(*expression);
  if (HasGlobal(command.globals, "--safe") && expression != nullptr && ContainsArmedAction(*expression)) {
    on_error("-delete", absl::FailedPreconditionError("refused: --safe forbids destructive actions"));
    return 2;  // do not traverse
  }
  WalkOptions options;
  options.symlinks = ResolveSymlinkMode(command.globals);
  const render::Format format = ResolveFormat(command.globals);
  const std::optional<std::string> tmpl = ResolveTemplate(command.globals);
  if (expression != nullptr) {
    ScanDepthOptions(*expression, &options);
  }
  // Capture one reference instant so every entry's age test (-mtime/-mmin) is
  // measured against the same clock, matching find's start-time semantics.
  const absl::Time now = absl::Now();
  int errors = 0;

  // --dry-run: route deletions through a previewing wrapper, so -delete reports
  // what it would remove without touching the filesystem.
  DryRunFileSystem dry_run_fs(fs, emit);
  const vfs::FileSystem& walk_fs = HasGlobal(command.globals, "--dry-run") ? dry_run_fs : fs;

  const absl::Status status = Walk(
      walk_fs, command.roots, options,
      [&](const Visit& visit) {
        Control control;
        const bool matched = expression == nullptr || Evaluate(*expression, visit, emit, walk_fs, now, control);
        if (matched && !has_action) {
          emit(tmpl.has_value() ? fields::Render(*tmpl, visit.path, visit.metadata, visit.depth) + "\n"
                                : render::Renderer(format).Record(visit.path));  // --template overrides --format
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
