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

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "xff/engine/walk.h"
#include "xff/parser/ast.h"
#include "xff/registry/descriptor.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

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

// Matches find's `-size N[bcwkMG]` with an optional +/- prefix. The file size
// is rounded UP to the chosen unit (default 512-byte blocks), as find does.
bool MatchesSize(std::string_view arg, std::uint64_t size_bytes) {
  char compare = '=';
  if (!arg.empty() && (arg.front() == '+' || arg.front() == '-')) {
    compare = arg.front();
    arg.remove_prefix(1);
  }
  if (arg.empty()) {
    return false;
  }
  std::uint64_t unit = 512;  // find default: 512-byte blocks
  const char suffix = arg.back();
  if (suffix < '0' || suffix > '9') {
    switch (suffix) {
      case 'b': unit = 512; break;
      case 'c': unit = 1; break;
      case 'w': unit = 2; break;
      case 'k': unit = 1024; break;
      case 'M': unit = 1024ULL * 1024; break;
      case 'G': unit = 1024ULL * 1024 * 1024; break;
      default: return false;  // unknown unit
    }
    arg.remove_suffix(1);
  }
  if (arg.empty()) {
    return false;
  }
  std::uint64_t want = 0;
  for (const char digit : arg) {
    if (digit < '0' || digit > '9') {
      return false;
    }
    want = want * 10 + static_cast<std::uint64_t>(digit - '0');
  }
  const std::uint64_t size_in_units = (size_bytes + unit - 1) / unit;
  if (compare == '+') {
    return size_in_units > want;
  }
  if (compare == '-') {
    return size_in_units < want;
  }
  return size_in_units == want;
}

// Matches a plain integer metadata field (e.g. -links) with an optional +/-
// prefix: `+N` greater than N, `-N` less than N, `N` exactly.
bool MatchesNumeric(std::string_view arg, std::uint64_t value) {
  char compare = '=';
  if (!arg.empty() && (arg.front() == '+' || arg.front() == '-')) {
    compare = arg.front();
    arg.remove_prefix(1);
  }
  if (arg.empty()) {
    return false;
  }
  std::uint64_t want = 0;
  for (const char digit : arg) {
    if (digit < '0' || digit > '9') {
      return false;
    }
    want = want * 10 + static_cast<std::uint64_t>(digit - '0');
  }
  if (compare == '+') {
    return value > want;
  }
  if (compare == '-') {
    return value < want;
  }
  return value == want;
}

// Matches find's `-perm` over the permission bits (incl. setuid/setgid/sticky):
//   MODE   exact match;  -MODE  all of MODE's bits set;  /MODE  any of them set.
// Octal MODE only for now (symbolic `u+w`,... is deferred).
bool MatchesPerm(std::string_view arg, std::uint32_t mode) {
  char op = '=';
  if (!arg.empty() && (arg.front() == '-' || arg.front() == '/')) {
    op = arg.front();
    arg.remove_prefix(1);
  }
  if (arg.empty()) {
    return false;
  }
  std::uint32_t want = 0;
  for (const char digit : arg) {
    if (digit < '0' || digit > '7') {
      return false;  // octal digits only
    }
    want = want * 8 + static_cast<std::uint32_t>(digit - '0');
  }
  const std::uint32_t bits = mode & 07777U;  // permission + setuid/setgid/sticky bits
  if (op == '-') {
    return (bits & want) == want;  // all requested bits set
  }
  if (op == '/') {
    return want == 0 || (bits & want) != 0;  // any requested bit set
  }
  return bits == want;  // exact
}

// find's -empty: an empty regular file (size 0) or a directory with no entries.
bool IsEmpty(const Visit& visit, const vfs::FileSystem& fs) {
  if (visit.metadata.type == vfs::FileType::kRegular) {
    return visit.metadata.size == 0;
  }
  if (visit.metadata.type == vfs::FileType::kDirectory) {
    const absl::StatusOr<std::vector<vfs::Entry>> children = fs.ReadDir(visit.path);
    return children.ok() && children->empty();
  }
  return false;  // find -empty matches only empty regular files and directories
}

// find's -newer FILE: the entry was modified more recently than FILE. FILE is
// stat'd (following symlinks); a missing/unreadable reference makes it false.
// (FILE is re-stat'd per entry for now; resolving it once is a later optimization.)
bool IsNewerThan(const Visit& visit, std::string_view reference, const vfs::FileSystem& fs) {
  const absl::StatusOr<vfs::Metadata> ref = fs.Stat(reference, /*follow_symlinks=*/true);
  return ref.ok() && visit.metadata.mtime > ref->mtime;
}

bool EvaluatePredicate(const parser::Expr& expr, const Visit& visit, EmitFn emit, const vfs::FileSystem& fs) {
  const std::string_view name = expr.descriptor->name;
  const bool has_arg = !expr.args.empty();
  if (name == "-true") return true;
  if (name == "-false") return false;
  if (name == "-name") return has_arg && Fnmatch(expr.args.front(), visit.name, 0);
  if (name == "-iname") return has_arg && Fnmatch(expr.args.front(), visit.name, FNM_CASEFOLD);
  if (name == "-path") return has_arg && Fnmatch(expr.args.front(), visit.path, 0);
  if (name == "-ipath") return has_arg && Fnmatch(expr.args.front(), visit.path, FNM_CASEFOLD);
  if (name == "-type") return has_arg && MatchesType(expr.args.front(), visit.metadata.type);
  if (name == "-size") return has_arg && MatchesSize(expr.args.front(), visit.metadata.size);
  if (name == "-links") return has_arg && MatchesNumeric(expr.args.front(), visit.metadata.nlink);
  if (name == "-perm") return has_arg && MatchesPerm(expr.args.front(), visit.metadata.mode);
  if (name == "-empty") return IsEmpty(visit, fs);
  if (name == "-newer") return has_arg && IsNewerThan(visit, expr.args.front(), fs);
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

bool Evaluate(const parser::Expr& expr, const Visit& visit, EmitFn emit, const vfs::FileSystem& fs) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: return EvaluatePredicate(expr, visit, emit, fs);
    case parser::Expr::Kind::kNot: return !Evaluate(*expr.lhs, visit, emit, fs);
    case parser::Expr::Kind::kAnd: return Evaluate(*expr.lhs, visit, emit, fs) && Evaluate(*expr.rhs, visit, emit, fs);
    case parser::Expr::Kind::kOr: return Evaluate(*expr.lhs, visit, emit, fs) || Evaluate(*expr.rhs, visit, emit, fs);
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
