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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "mbo/container/limited_map.h"
#include "xff/datetime/datetime.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/walk.h"
#include "xff/exec/exec.h"
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
      if (expr.descriptor->name == "-depth" || expr.descriptor->name == "-d" || expr.descriptor->name == "-delete") {
        options->post_order = true;  // -delete implies -depth; -d is the BSD/GNU short spelling
        break;
      }
      if (expr.descriptor->name == "-xdev" || expr.descriptor->name == "-mount" || expr.descriptor->name == "-x") {
        options->single_filesystem = true;  // -mount (GNU/BSD) and -x (BSD) are synonyms for -xdev
        break;
      }
      if (expr.descriptor->name == "-ignore_readdir_race") {
        options->ignore_readdir_race = true;
        break;
      }
      if (expr.descriptor->name == "-noignore_readdir_race") {
        options->ignore_readdir_race = false;  // last occurrence wins, as in find
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
    case parser::Expr::Kind::kNot: ScanDepthOptions(*expr.lhs, options); break;
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

// The mode-scoped default worker count when `-j` is absent (docs/design-parallel.md
// "Parallelism control"): modern (kXff) leaves a core for the consumer and caps at
// 15 to avoid oversubscription; find/fd/rg saturate cores; an unset style stays
// sequential (the conservative in-process / test default).
std::size_t DefaultWorkers(std::optional<registry::Style> style) {
  if (!style.has_value()) {
    return 1;
  }
  const unsigned detected = std::thread::hardware_concurrency();
  const std::size_t cores = detected == 0 ? 1 : detected;
  if (*style == registry::Style::kXff) {
    return std::max<std::size_t>(1, std::min<std::size_t>(cores - 1, 15));
  }
  return cores;
}

// xff --sort=none|dir|subtree|tree: per-directory sibling ordering for the walk
// (see docs/design-parallel.md). `none` keeps readdir order (find's default);
// `dir` sorts each directory's listing; `subtree` adds contiguous subtrees;
// `tree` is a total path order. Bare --sort and the legacy `name` mean `dir`. The
// default is mode-scoped: modern (kXff) sorts each directory, find stays unordered.
// Leading global, last occurrence wins.
SortOrder ResolveSort(const std::vector<std::string>& globals, std::optional<registry::Style> style) {
  SortOrder sort = style == registry::Style::kXff ? SortOrder::kDir : SortOrder::kNone;
  for (const std::string& global : globals) {
    if (global == "--sort" || global == "--sort=dir" || global == "--sort=name") {
      sort = SortOrder::kDir;
    } else if (global == "--sort=subtree") {
      sort = SortOrder::kSubtree;
    } else if (global == "--sort=tree") {
      sort = SortOrder::kTree;
    } else if (global == "--sort=none") {
      sort = SortOrder::kNone;
    }
  }
  return sort;
}

// xff -jN / --jobs=N: worker threads for the parallel directory read-ahead (see
// docs/design-parallel.md). When absent, the count is mode-scoped (DefaultWorkers).
// Leading global, last valid occurrence wins; a non-positive or unparseable value
// is ignored.
std::size_t ResolveJobs(const std::vector<std::string>& globals, std::optional<registry::Style> style) {
  std::size_t jobs = DefaultWorkers(style);
  for (const std::string& global : globals) {
    std::string_view value;
    if (global.starts_with("--jobs=")) {
      value = std::string_view(global).substr(7);
    } else if (global.starts_with("-j") && global.size() > 2) {
      value = std::string_view(global).substr(2);
    } else {
      continue;
    }
    if (value == "all") {  // --jobs=all / -jall: every detected core, regardless of mode
      const unsigned detected = std::thread::hardware_concurrency();
      jobs = detected == 0 ? 1 : detected;
      continue;
    }
    std::size_t parsed = 0;
    if (absl::SimpleAtoi(value, &parsed) && parsed >= 1) {
      jobs = parsed;
    }
  }
  return jobs;
}

// xff --summary[=overall|type|ext]: reduce the matches to a count + total size
// table instead of printing each one. Bare --summary / =overall is one total row;
// =type groups by file type, =ext by filename extension; =none / absent is off.
enum class SummaryMode { kOff, kOverall, kType, kExt };

SummaryMode ResolveSummary(const std::vector<std::string>& globals) {
  SummaryMode mode = SummaryMode::kOff;
  for (const std::string& global : globals) {
    if (global == "--summary" || global == "--summary=overall") {
      mode = SummaryMode::kOverall;
    } else if (global == "--summary=type") {
      mode = SummaryMode::kType;
    } else if (global == "--summary=ext") {
      mode = SummaryMode::kExt;
    } else if (global == "--summary=none") {
      mode = SummaryMode::kOff;
    }
  }
  return mode;
}

// The readable file-type word used as a --summary=type group key, keyed by file
// type (kUnknown and any unmapped value fall through to "unknown"). A constexpr map,
// per the style's preference for a uniform key -> value mapping over a switch.
using TypeNamePair = std::pair<vfs::FileType, std::string_view>;
constexpr auto kTypeNames = mbo::container::MakeLimitedMap(
    TypeNamePair{vfs::FileType::kBlockDevice, "block-device"},
    TypeNamePair{vfs::FileType::kCharDevice, "char-device"},
    TypeNamePair{vfs::FileType::kDirectory, "directory"},
    TypeNamePair{vfs::FileType::kFifo, "fifo"},
    TypeNamePair{vfs::FileType::kRegular, "file"},
    TypeNamePair{vfs::FileType::kSocket, "socket"},
    TypeNamePair{vfs::FileType::kSymlink, "symlink"});

std::string_view TypeName(vfs::FileType type) {
  const auto it = kTypeNames.find(type);
  return it == kTypeNames.end() ? "unknown" : it->second;  // kUnknown / unmapped -> "unknown"
}

// The filename extension used as a --summary=ext group key: the part after the
// last '.', or "(none)" when there is none (including a leading-dot dotfile).
std::string SummaryExtension(std::string_view name) {
  const std::string_view::size_type dot = name.rfind('.');
  if (dot == std::string_view::npos || dot == 0) {
    return "(none)";
  }
  return std::string(name.substr(dot + 1));
}

// The group key for one matched entry under `mode` (kOff never reaches here).
std::string SummaryKey(SummaryMode mode, const Visit& visit) {
  switch (mode) {
    case SummaryMode::kExt: return SummaryExtension(visit.name);
    case SummaryMode::kType: return std::string(TypeName(visit.metadata.type));
    default: return "total";  // kOverall: a single bucket
  }
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

// --timezone=ZONE (short alias --tz=ZONE) overrides the detected local zone used to
// interpret time-string arguments (-newerXt). Last occurrence of either spelling
// wins. Resolves the winner to *tz and returns true; an unknown zone returns false
// with *bad set to the offending value (and *tz unchanged), so the caller can fail
// the run. Absent the flag, leaves *tz at its local-zone default and returns true.
bool ResolveTimeZone(const std::vector<std::string>& globals, absl::TimeZone* tz, std::string* bad) {
  std::optional<std::string> spec;
  for (const std::string& global : globals) {
    for (const std::string_view prefix : {std::string_view("--timezone="), std::string_view("--tz=")}) {
      if (global.starts_with(prefix)) {
        spec = global.substr(prefix.size());  // last occurrence of either spelling wins
      }
    }
  }
  if (!spec.has_value()) {
    return true;
  }
  if (!datetime::ParseTimeZone(*spec, tz)) {
    *bad = *std::move(spec);
    return false;
  }
  return true;
}

// --block-size=SIZE sets the bytes-per-block for a bare `-size N` and `-size Nb`
// (find's historical default is 512). Last occurrence wins. Resolves to *block_size
// (left untouched when the flag is absent) and returns Ok, or the parse error.
absl::Status ResolveBlockSize(const std::vector<std::string>& globals, std::uint64_t* block_size) {
  constexpr std::string_view kPrefix = "--block-size=";
  std::optional<std::string> spec;
  for (const std::string& global : globals) {
    if (global.starts_with(kPrefix)) {
      spec = global.substr(kPrefix.size());  // last occurrence wins
    }
  }
  if (!spec.has_value()) {
    return absl::OkStatus();
  }
  const absl::StatusOr<std::uint64_t> bytes = ParseBlockSize(*spec);
  if (!bytes.ok()) {
    return bytes.status();
  }
  *block_size = *bytes;
  return absl::OkStatus();
}

// --time-format=NAME sets the default format for a time field rendered without an
// explicit {:qualifier} (a datetime preset name or a custom absl::FormatTime
// pattern). Last occurrence wins; empty (absent) keeps the built-in "space"
// default. Any value is accepted verbatim (an unknown name renders literally,
// like printf), so there is nothing to reject.
std::string ResolveTimeFormat(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--time-format=";
  std::string format;
  for (const std::string& global : globals) {
    if (global.starts_with(kPrefix)) {
      format = global.substr(kPrefix.size());  // last occurrence wins
    }
  }
  return format;
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

  bool Access(std::string_view path, vfs::AccessMode mode) const override { return fs_.Access(path, mode); }

  absl::StatusOr<std::string> ReadLink(std::string_view path) const override { return fs_.ReadLink(path); }

  absl::StatusOr<std::string> FsType(std::string_view path) const override { return fs_.FsType(path); }

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
    case parser::Expr::Kind::kPredicate: return expr.descriptor->name == "-delete" || expr.descriptor->name == "-exec";
    case parser::Expr::Kind::kNot: return ContainsArmedAction(*expr.lhs);
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kComma: return ContainsArmedAction(*expr.lhs) || ContainsArmedAction(*expr.rhs);
  }
  return false;
}

// True if the expression mentions the primary `name` anywhere. Used for the
// positional options that take effect run-wide regardless of position (-daystart).
bool ContainsPrimary(const parser::Expr& expr, std::string_view name) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: return expr.descriptor->name == name;
    case parser::Expr::Kind::kNot: return ContainsPrimary(*expr.lhs, name);
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kComma: return ContainsPrimary(*expr.lhs, name) || ContainsPrimary(*expr.rhs, name);
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

// --implicit-print=yes|no forces the default (implicit) print on or off,
// overriding find's "an action suppresses it" rule. Last occurrence wins;
// nullopt means no override (use the find default). Bare --implicit-print == =yes.
std::optional<bool> ResolveImplicitPrint(const std::vector<std::string>& globals) {
  std::optional<bool> result;
  for (const std::string& global : globals) {
    if (global == "--implicit-print" || global == "--implicit-print=yes") {
      result = true;
    } else if (global == "--implicit-print=no") {
      result = false;
    }
  }
  return result;
}

// Collects --define=NAME=VALUE globals into a name->value map (last wins). The
// text after the prefix is NAME=VALUE; NAME runs to the first '=', VALUE (which
// may itself contain '=') is the rest. --define=NAME with no '=' binds empty.
std::map<std::string, std::string> ResolveDefines(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--define=";
  std::map<std::string, std::string> defines;
  for (const std::string& global : globals) {
    if (!global.starts_with(kPrefix)) {
      continue;
    }
    const std::string spec = global.substr(kPrefix.size());
    const std::string::size_type eq = spec.find('=');
    if (eq == std::string::npos) {
      defines[spec] = "";
    } else {
      defines[spec.substr(0, eq)] = spec.substr(eq + 1);
    }
  }
  return defines;
}

// Whether --capture-override permits re-binding a -capture NAME. Strict by
// default (a duplicate name is an error); --capture-override (== =yes) allows it,
// --capture-override=no restores strict. Last occurrence wins.
bool CaptureOverride(const std::vector<std::string>& globals) {
  bool allow = false;
  for (const std::string& global : globals) {
    if (global == "--capture-override" || global == "--capture-override=yes") {
      allow = true;
    } else if (global == "--capture-override=no") {
      allow = false;
    }
  }
  return allow;
}

// Collects the NAME of every -capture action in the expression (its args[0]).
void CollectCaptureNames(const parser::Expr& expr, std::vector<std::string>* names) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate:
      if (expr.descriptor->name == "-capture" && !expr.args.empty()) {
        names->push_back(expr.args.front());
      }
      break;
    case parser::Expr::Kind::kNot: CollectCaptureNames(*expr.lhs, names); break;
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kComma:
      CollectCaptureNames(*expr.lhs, names);
      CollectCaptureNames(*expr.rhs, names);
      break;
  }
}

// Returns a -capture NAME bound more than once, or nullopt when all are unique.
std::optional<std::string> DuplicateCaptureName(const parser::Expr& expr) {
  std::vector<std::string> names;
  CollectCaptureNames(expr, &names);
  std::sort(names.begin(), names.end());
  const auto dup = std::adjacent_find(names.begin(), names.end());
  return dup == names.end() ? std::nullopt : std::optional<std::string>(*dup);
}

// Collects strings that may reference {capture.NAME}: the command tokens of every
// -exec and -capture action (a later command can use an earlier capture). The
// --template global is added by the caller.
void CollectCaptureRefs(const parser::Expr& expr, std::vector<std::string>* refs) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate:
      if (expr.descriptor->name == "-exec") {
        refs->insert(refs->end(), expr.args.begin(), expr.args.end());
      } else if (expr.descriptor->name == "-capture" && expr.args.size() > 2) {
        refs->insert(refs->end(), expr.args.begin() + 2, expr.args.end());  // skip [NAME, REGEX]
      }
      break;
    case parser::Expr::Kind::kNot: CollectCaptureRefs(*expr.lhs, refs); break;
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kComma:
      CollectCaptureRefs(*expr.lhs, refs);
      CollectCaptureRefs(*expr.rhs, refs);
      break;
  }
}

// Returns a -capture NAME whose {capture.NAME} placeholder appears nowhere (no
// -exec/-capture command and not the --template), or nullopt when all are used.
std::optional<std::string> UnusedCaptureName(const parser::Expr& expr, const std::optional<std::string>& tmpl) {
  std::vector<std::string> names;
  CollectCaptureNames(expr, &names);
  if (names.empty()) {
    return std::nullopt;
  }
  std::vector<std::string> refs;
  CollectCaptureRefs(expr, &refs);
  if (tmpl.has_value()) {
    refs.push_back(*tmpl);
  }
  for (const std::string& name : names) {
    const std::string closed = absl::StrCat("{capture.", name, "}");
    const std::string qualified = absl::StrCat("{capture.", name, ":");
    const bool used = std::any_of(refs.begin(), refs.end(), [&](const std::string& ref) {
      return ref.find(closed) != std::string::npos || ref.find(qualified) != std::string::npos;
    });
    if (!used) {
      return name;
    }
  }
  return std::nullopt;
}

}  // namespace

int RunFind(
    const parser::Command& command,
    const vfs::FileSystem& fs,
    EmitFn emit,
    WalkErrorFn on_error,
    std::optional<registry::Style> style,
    bool* any_match) {
  if (any_match != nullptr) {
    *any_match = false;  // no match until an entry satisfies the expression
  }
  const parser::Expr* const expression = command.expression.get();
  const bool has_action = expression != nullptr && ContainsAction(*expression);
  // --implicit-print=yes|no overrides find's default-print rule (otherwise !has_action).
  const bool implicit_print = ResolveImplicitPrint(command.globals).value_or(!has_action);
  if (HasGlobal(command.globals, "--safe") && expression != nullptr && ContainsArmedAction(*expression)) {
    on_error("-delete", absl::FailedPreconditionError("refused: --safe forbids destructive actions"));
    return 2;  // do not traverse
  }
  // A -capture NAME bound twice is an error by default (silent clobbering would
  // mean silently-wrong data); --capture-override opts into last-wins.
  if (expression != nullptr && !CaptureOverride(command.globals)) {
    if (const std::optional<std::string> dup = DuplicateCaptureName(*expression); dup.has_value()) {
      on_error(
          "-capture",
          absl::FailedPreconditionError(absl::StrCat("duplicate -capture name '", *dup, "'; use --capture-override")));
      return 2;  // do not traverse
    }
  }
  WalkOptions options;
  options.symlinks = ResolveSymlinkMode(command.globals);
  options.sort = ResolveSort(command.globals, style);
  options.workers = ResolveJobs(command.globals, style);
  const render::Format format = ResolveFormat(command.globals);
  const std::optional<std::string> tmpl = ResolveTemplate(command.globals);
  // A -capture whose {capture.NAME} is never referenced ran a subprocess for
  // nothing (use -exec for pure side effects); flag it before traversing.
  if (expression != nullptr) {
    if (const std::optional<std::string> unused = UnusedCaptureName(*expression, tmpl); unused.has_value()) {
      on_error(
          "-capture", absl::FailedPreconditionError(
                          absl::StrCat("-capture '", *unused, "' is never referenced as {capture.", *unused, "}")));
      return 2;  // do not traverse
    }
  }
  // Precompile the --template once; rendering each match then skips re-scanning.
  const std::optional<fields::Template> compiled_tmpl =
      tmpl.has_value() ? std::optional<fields::Template>(fields::Template::Compile(*tmpl)) : std::nullopt;
  const bool exec_fields = HasGlobal(command.globals, "--exec-fields");  // route -exec through the vocabulary
  const std::map<std::string, std::string> defines = ResolveDefines(command.globals);  // {def.NAME} values
  if (expression != nullptr) {
    ScanDepthOptions(*expression, &options);
  }
  // A malformed -size value (unknown unit, an over-64-bit unit like Z/Y, or a
  // non-numeric count) is a usage error refused before the walk -- find rejects bad
  // -size at parse time too, rather than silently matching nothing.
  if (expression != nullptr) {
    if (const absl::Status size_status = ValidateSizeArgs(*expression); !size_status.ok()) {
      on_error("-size", size_status);
      return 2;  // do not traverse
    }
  }
  // --timezone=ZONE overrides the local zone for interpreting time-string args
  // (-newerXt) and -daystart's midnight. Resolved first (both need it); an unknown
  // zone is a usage error, refused before traversal.
  absl::TimeZone tz = absl::LocalTimeZone();
  if (std::string bad; !ResolveTimeZone(command.globals, &tz, &bad)) {
    on_error("--timezone", absl::InvalidArgumentError(absl::StrCat("unknown time zone: '", bad, "'")));
    return 2;  // do not traverse
  }
  // Capture one reference instant so every entry's age test (-mtime/-mmin) is
  // measured against the same clock. -daystart measures from today's local
  // midnight (in tz) instead of find's start time (the run's start).
  const bool daystart = expression != nullptr && ContainsPrimary(*expression, "-daystart");
  const absl::Time now = daystart ? datetime::StartOfDay(absl::Now(), tz) : absl::Now();
  // --time-format=NAME: default spec for a time field with no {:qualifier}.
  const std::string time_format = ResolveTimeFormat(command.globals);
  // --block-size=SIZE: bytes per -size block (a bare value / the 'b' suffix); find's
  // historical default is 512. A malformed SIZE is a usage error, refused here.
  std::uint64_t block_size = 512;
  if (const absl::Status size_status = ResolveBlockSize(command.globals, &block_size); !size_status.ok()) {
    on_error("--block-size", size_status);
    return 2;  // do not traverse
  }
  // --summary: reduce matches to a {count, total size} per group instead of
  // printing each one; the table is emitted after the walk.
  const SummaryMode summary_mode = ResolveSummary(command.globals);
  std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> summary;  // group -> {count, total size}
  int errors = 0;

  // --dry-run: route deletions through a previewing wrapper, so -delete reports
  // what it would remove without touching the filesystem.
  const DryRunFileSystem dry_run_fs(fs, emit);
  const vfs::FileSystem& walk_fs = HasGlobal(command.globals, "--dry-run") ? dry_run_fs : fs;

  // -ok confirmation: prompt to stderr, read a line from stdin, affirmative on y/Y (like find).
  const auto confirm = [](std::string_view prompt) -> bool {
    std::cerr << prompt;
    std::string line;
    if (!std::getline(std::cin, line)) {
      return false;  // EOF or closed stdin -> decline
    }
    return !line.empty() && (line[0] == 'y' || line[0] == 'Y');
  };

  // File-output actions (-fprint/-fprint0/-fprintf/-fls) append to a named file,
  // opened once (truncating) on first write and held open for the whole walk. The
  // visitor is single-threaded, so the sink map needs no synchronisation. Streams
  // close (flushing) when `file_sinks` goes out of scope after the walk.
  std::map<std::string, std::ofstream> file_sinks;
  const auto emit_file = [&file_sinks](std::string_view file, std::string_view record) {
    const std::string name(file);
    auto it = file_sinks.find(name);
    if (it == file_sinks.end()) {
      it = file_sinks.emplace(name, std::ofstream(name, std::ios::binary | std::ios::trunc)).first;
    }
    it->second.write(record.data(), static_cast<std::streamsize>(record.size()));
  };

  // `-exec/-execdir ... +`: each batch node's matched items accrue here during the
  // walk and run at the end. Outer key the Expr node; inner key the directory ("" =
  // -exec's single global batch, the entry's dir = -execdir's per-dir batches). The
  // visitor is single-threaded, so no synchronisation is needed.
  std::map<const parser::Expr*, std::map<std::string, std::vector<std::string>>> exec_batches;

  // -j>1: `-exec/-execdir ... ;` children run concurrently on this bounded runner,
  // capped at the same worker count as the walk (docs/design-parallel.md's single
  // knob). It is wired into the context only when workers > 1; at -j1 (and the
  // in-process default) the actions stay synchronous and this stays idle.
  exec::ParallelExec parallel_exec(options.workers);

  // Impossible-task policy (design.md "Exit-code model"): a predicate that cannot be
  // evaluated correctly on an entry's filesystem (e.g. -Btime where birth time is
  // unrecorded) signals via control.unsupported. By default that is a hard error
  // (exit 2); --skip-unsupported downgrades it to a warning and skips the entry.
  // Reported once per run (a representative path) rather than once per entry, so a
  // whole btime-less tree does not flood stderr.
  const bool skip_unsupported = HasGlobal(command.globals, "--skip-unsupported");
  bool unsupported_reported = false;

  const absl::Status status = Walk(
      walk_fs, command.roots, options,
      [&](const Visit& visit) {
        Control control;
        std::vector<std::string> captures;           // -regex groups for this entry; consumed by gated -exec {0}..{N}
        std::map<std::string, std::string> outputs;  // -capture results for this entry; read by {capture.NAME}
        EvalContext eval_context{
            .visit = visit,
            .emit = emit,
            .emit_file = emit_file,
            .fs = walk_fs,
            .now = now,
            .tz = tz,
            .time_format = time_format,
            .block_size = block_size,
            .control = control,
            .exec_fields = exec_fields,
            .captures = exec_fields ? &captures : nullptr,
            .defines = &defines,
            .outputs = &outputs,
            .confirm = confirm,
            .exec_batches = &exec_batches,
            .parallel_exec = options.workers > 1 ? &parallel_exec : nullptr};
        const bool matched = expression == nullptr || Evaluate(*expression, eval_context);
        if (matched && any_match != nullptr) {
          *any_match = true;  // grep-style "found anything" -- the expression's truth, not output
        }
        if (matched && summary_mode != SummaryMode::kOff) {
          // --summary reduces matches instead of printing them: bump this group's
          // count and size. Explicit actions (-print/-exec) still ran via Evaluate.
          std::pair<std::uint64_t, std::uint64_t>& agg = summary[SummaryKey(summary_mode, visit)];
          agg.first += 1;
          agg.second += visit.metadata.size;
        } else if (matched && implicit_print) {
          if (compiled_tmpl.has_value()) {  // --template overrides --format
            emit(
                compiled_tmpl->Render(
                    fields::RenderContext{
                        .path = visit.path,
                        .root = visit.root,
                        .metadata = visit.metadata,
                        .depth = visit.depth,
                        .tz = tz,
                        .time_format = time_format,
                        .defines = &defines,
                        .outputs = &outputs})
                + "\n");
          } else {
            emit(render::Renderer(format).Record(visit.path));
          }
        }
        if (!control.unsupported.empty() && !unsupported_reported) {
          unsupported_reported = true;  // once per run, not per entry
          if (skip_unsupported) {
            on_error(visit.path, absl::FailedPreconditionError(absl::StrCat(control.unsupported, " (skipped)")));
          } else {
            on_error(
                visit.path, absl::FailedPreconditionError(
                                absl::StrCat(control.unsupported, "; use --skip-unsupported to skip such entries")));
            ++errors;  // impossible task -> hard error (exit 2)
          }
        }
        if (control.quit) {
          return WalkAction::kStop;
        }
        if (control.prune) {
          return WalkAction::kPrune;
        }
        return WalkAction::kContinue;
      },
      [&](std::string_view path, absl::Status error_status) {
        ++errors;
        on_error(path, error_status);
      });
  if (!status.ok()) {
    ++errors;  // Fatal traversal error (none today; per-path errors handled above).
  }

  // -j>1: reap every concurrent `-exec/-execdir ... ;` child still running. find's
  // `;` form is a predicate -- a nonzero exit makes only the action false, it does
  // NOT affect find's exit status (verified against BSD/GNU find) -- so the drained
  // failure count is intentionally discarded here, keeping -jN identical to the
  // synchronous -j1 path. The `+` batch form is the one that does count failures,
  // and it runs through exec_batches just below. A no-op when nothing was launched.
  parallel_exec.Drain();

  // `-exec/-execdir ... +`: now that the walk is done, run each batch node's
  // accumulated items in ARG_MAX chunks -- -exec once over the global ("") bucket,
  // -execdir once per directory bucket (cwd = that dir). A nonzero exit is a
  // per-command error, as for `;`.
  for (const auto& [node, by_dir] : exec_batches) {
    const bool execdir = node->descriptor->name == "-execdir";
    for (const auto& [dir, items] : by_dir) {
      const bool ok = execdir ? exec::ExecuteBatchInDir(node->args, items, dir) : exec::ExecuteBatch(node->args, items);
      if (!ok) {
        ++errors;
        on_error(node->descriptor->name, absl::UnknownError("batched command exited non-zero"));
      }
    }
  }

  // --summary: emit the accumulated table -- one `group<TAB>count<TAB>bytes` row
  // per group (sorted by key, since `summary` is an ordered map), then a `total`
  // row. The overall mode has a single group already keyed "total".
  if (summary_mode != SummaryMode::kOff) {
    std::uint64_t total_count = 0;
    std::uint64_t total_size = 0;
    for (const auto& [key, agg] : summary) {
      emit(absl::StrCat(key, "\t", agg.first, "\t", agg.second, "\n"));
      total_count += agg.first;
      total_size += agg.second;
    }
    if (summary_mode != SummaryMode::kOverall) {
      emit(absl::StrCat("total\t", total_count, "\t", total_size, "\n"));
    }
  }

  return errors;
}

}  // namespace xff::engine
