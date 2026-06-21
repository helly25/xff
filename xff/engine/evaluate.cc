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
#include <grp.h>
#include <pwd.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "xff/engine/walk.h"
#include "xff/exec/exec.h"
#include "xff/fields/fields.h"
#include "xff/parser/ast.h"
#include "xff/regex/regex.h"
#include "xff/registry/descriptor.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

// The OS-native line terminator used by -println/-printfln (xff extensions).
// LF today; a Windows build would select "\r\n". Centralized so both actions
// agree and the platform choice lives in one place.
constexpr std::string_view kOsLineEnding = "\n";

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

// find's %y type letter: the inverse of MatchesType's mapping.
char TypeLetter(vfs::FileType type) {
  switch (type) {
    case vfs::FileType::kRegular: return 'f';
    case vfs::FileType::kDirectory: return 'd';
    case vfs::FileType::kSymlink: return 'l';
    case vfs::FileType::kBlockDevice: return 'b';
    case vfs::FileType::kCharDevice: return 'c';
    case vfs::FileType::kFifo: return 'p';
    case vfs::FileType::kSocket: return 's';
    case vfs::FileType::kUnknown: return 'U';
  }
  return 'U';
}

// The directory component of `path` (find's %h): everything before the last
// '/', "/" for a root-level child, or "." when there is no '/'.
std::string_view Dirname(std::string_view path) {
  const std::string_view::size_type slash = path.rfind('/');
  if (slash == std::string_view::npos) return ".";
  if (slash == 0) return "/";
  return path.substr(0, slash);
}

// Permission bits as octal without leading zeros (find's %m): %o of mode & 07777.
std::string OctalPerm(std::uint32_t mode) {
  const unsigned bits = mode & 07777U;
  std::string out;
  for (int shift = 9; shift >= 0; shift -= 3) {
    const unsigned digit = (bits >> shift) & 7U;
    if (!out.empty() || digit != 0) {
      out.push_back(static_cast<char>('0' + digit));
    }
  }
  return out.empty() ? "0" : out;
}

// find's -printf FORMAT: expands % directives and \ escapes against the entry.
// Supported: %p path, %f name, %h dir, %s size, %m octal perm, %d depth, %y
// type, %i inode, %n links, %% literal; \n \t \r \\ \0 escapes. Unknown
// directives are emitted literally. (%u/%g/%t and friends are a follow-up.)
std::string FormatPrintf(std::string_view format, const Visit& visit) {
  std::string out;
  for (std::string_view::size_type i = 0; i < format.size(); ++i) {
    const char ch = format[i];
    if (ch == '\\' && i + 1 < format.size()) {
      switch (format[++i]) {
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        case 'r': out.push_back('\r'); break;
        case '0': out.push_back('\0'); break;
        case '\\': out.push_back('\\'); break;
        default: out.push_back('\\'); out.push_back(format[i]); break;
      }
    } else if (ch == '%' && i + 1 < format.size()) {
      switch (format[++i]) {
        case 'p': out.append(visit.path); break;
        case 'f': out.append(visit.name); break;
        case 'h': out.append(Dirname(visit.path)); break;
        case 's': absl::StrAppend(&out, visit.metadata.size); break;
        case 'm': out.append(OctalPerm(visit.metadata.mode)); break;
        case 'd': absl::StrAppend(&out, visit.depth); break;
        case 'y': out.push_back(TypeLetter(visit.metadata.type)); break;
        case 'i': absl::StrAppend(&out, visit.metadata.ino); break;
        case 'n': absl::StrAppend(&out, visit.metadata.nlink); break;
        case '%': out.push_back('%'); break;
        default: out.push_back('%'); out.push_back(format[i]); break;
      }
    } else {
      out.push_back(ch);
    }
  }
  return out;
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

// Selects a timestamp by find's X/Y letter: a=access, c=inode-change, m=modify.
absl::Time TimeField(const vfs::Metadata& metadata, char field) {
  switch (field) {
    case 'a': return metadata.atime;
    case 'c': return metadata.ctime;
    default: return metadata.mtime;  // 'm'
  }
}

// find's -newerXY (X,Y in {a,c,m}): the entry's X-time is more recent than the
// reference FILE's Y-time. The reference is stat'd following symlinks; a missing
// reference makes it false. (The Y=t time-string form is a later addition.)
bool IsNewerXY(const Visit& visit, char x, char y, std::string_view reference, const vfs::FileSystem& fs) {
  const absl::StatusOr<vfs::Metadata> ref = fs.Stat(reference, /*follow_symlinks=*/true);
  return ref.ok() && TimeField(visit.metadata, x) > TimeField(*ref, y);
}

// find's -mtime/-mmin: the entry was modified N units ago -- 24h for -mtime, one
// minute for -mmin -- with any fractional unit discarded (floor), so a 2.9-day
// file is "2 days". +N means strictly more than N units ago, -N strictly fewer.
bool MatchesTime(std::string_view arg, absl::Time mtime, absl::Time now, absl::Duration unit) {
  char compare = '=';
  if (!arg.empty() && (arg.front() == '+' || arg.front() == '-')) {
    compare = arg.front();
    arg.remove_prefix(1);
  }
  if (arg.empty()) {
    return false;
  }
  std::int64_t want = 0;
  for (const char digit : arg) {
    if (digit < '0' || digit > '9') {
      return false;
    }
    want = want * 10 + (digit - '0');
  }
  const std::int64_t units = static_cast<std::int64_t>((now - mtime) / unit);
  if (compare == '+') {
    return units > want;
  }
  if (compare == '-') {
    return units < want;
  }
  return units == want;
}

// Parses an all-digits string as an unsigned id, or nullopt otherwise.
std::optional<std::uint32_t> ParseId(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }
  std::uint32_t id = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    id = id * 10 + static_cast<std::uint32_t>(c - '0');
  }
  return id;
}

// find's -user/-group NAME: resolve NAME via the user/group database and compare
// to the entry's owner. If NAME is not a known user/group but is all-digits it is
// taken as a literal id (GNU find behaviour); an unknown non-numeric name yields
// no match here (failing the run on it is deferred to the exit-code work).
std::optional<std::uint32_t> ResolveUid(std::string_view name) {
  if (const struct passwd* const pw = ::getpwnam(std::string(name).c_str()); pw != nullptr) {
    return static_cast<std::uint32_t>(pw->pw_uid);
  }
  return ParseId(name);
}

std::optional<std::uint32_t> ResolveGid(std::string_view name) {
  if (const struct group* const gr = ::getgrnam(std::string(name).c_str()); gr != nullptr) {
    return static_cast<std::uint32_t>(gr->gr_gid);
  }
  return ParseId(name);
}

// find's -regex/-iregex: the pattern must match the whole path (not a substring).
// Compiled per call for now; pre-compilation is a tracked optimization. An
// uncompilable pattern matches nothing (a hard error is deferred to exit codes).
// When `captures` is non-null (gated -exec is active) a match records its groups
// there ([0] the whole match, 1..N the groups) for the {0}..{N} placeholders.
bool MatchesRegex(
    std::string_view pattern, std::string_view path, bool case_insensitive, std::vector<std::string>* captures) {
  const absl::StatusOr<regex::Matcher> matcher = regex::Matcher::Compile(pattern, case_insensitive);
  if (!matcher.ok()) {
    return false;
  }
  if (captures == nullptr) {
    return matcher->FullMatch(path);
  }
  std::optional<std::vector<std::string>> groups = matcher->FullMatchCaptures(path);
  if (!groups.has_value()) {
    return false;
  }
  *captures = std::move(*groups);
  return true;
}

// Applies a --capture extraction regex to `text` (the captured stdout): returns
// capture group 1, or the whole match when the regex has no groups, or empty
// when it does not fully match (or fails to compile).
std::string ExtractCapture(std::string_view regex, std::string_view text) {
  const absl::StatusOr<regex::Matcher> matcher = regex::Matcher::Compile(regex, /*case_insensitive=*/false);
  if (!matcher.ok()) {
    return "";
  }
  const std::optional<std::vector<std::string>> groups = matcher->FullMatchCaptures(text);
  if (!groups.has_value()) {
    return "";
  }
  return groups->size() > 1 ? (*groups)[1] : (*groups)[0];
}

bool EvaluatePredicate(const parser::Expr& expr, EvalContext& ctx) {
  // Alias the context members so the predicate/action body below reads them
  // directly (the body predates EvalContext and is left untouched).
  const Visit& visit = ctx.visit;
  const EmitFn emit = ctx.emit;
  const vfs::FileSystem& fs = ctx.fs;
  const absl::Time now = ctx.now;
  Control& control = ctx.control;
  const std::string_view name = expr.descriptor->name;
  const bool has_arg = !expr.args.empty();
  if (name == "-true") return true;
  if (name == "-false") return false;
  if (name == "-name") return has_arg && Fnmatch(expr.args.front(), visit.name, 0);
  if (name == "-iname") return has_arg && Fnmatch(expr.args.front(), visit.name, FNM_CASEFOLD);
  if (name == "-path") return has_arg && Fnmatch(expr.args.front(), visit.path, 0);
  if (name == "-ipath") return has_arg && Fnmatch(expr.args.front(), visit.path, FNM_CASEFOLD);
  if (name == "-regex") return has_arg && MatchesRegex(expr.args.front(), visit.path, false, ctx.captures);
  if (name == "-iregex") return has_arg && MatchesRegex(expr.args.front(), visit.path, true, ctx.captures);
  if (name == "-type") return has_arg && MatchesType(expr.args.front(), visit.metadata.type);
  if (name == "-size") return has_arg && MatchesSize(expr.args.front(), visit.metadata.size);
  if (name == "-links") return has_arg && MatchesNumeric(expr.args.front(), visit.metadata.nlink);
  if (name == "-uid") return has_arg && MatchesNumeric(expr.args.front(), visit.metadata.uid);
  if (name == "-gid") return has_arg && MatchesNumeric(expr.args.front(), visit.metadata.gid);
  if (name == "-user") {
    if (!has_arg) return false;
    const std::optional<std::uint32_t> uid = ResolveUid(expr.args.front());
    return uid.has_value() && visit.metadata.uid == *uid;
  }
  if (name == "-group") {
    if (!has_arg) return false;
    const std::optional<std::uint32_t> gid = ResolveGid(expr.args.front());
    return gid.has_value() && visit.metadata.gid == *gid;
  }
  if (name == "-perm") return has_arg && MatchesPerm(expr.args.front(), visit.metadata.mode);
  if (name == "-empty") return IsEmpty(visit, fs);
  if (name == "-newer") return has_arg && IsNewerThan(visit, expr.args.front(), fs);
  // -newerXY with X,Y in {a,c,m}: the registry only admits those 8-char names.
  if (name.size() == 8 && name.starts_with("-newer")) {
    return has_arg && IsNewerXY(visit, name[6], name[7], expr.args.front(), fs);
  }
  if (name == "-mtime") return has_arg && MatchesTime(expr.args.front(), visit.metadata.mtime, now, absl::Hours(24));
  if (name == "-mmin") return has_arg && MatchesTime(expr.args.front(), visit.metadata.mtime, now, absl::Minutes(1));
  if (name == "-atime") return has_arg && MatchesTime(expr.args.front(), visit.metadata.atime, now, absl::Hours(24));
  if (name == "-amin") return has_arg && MatchesTime(expr.args.front(), visit.metadata.atime, now, absl::Minutes(1));
  if (name == "-ctime") return has_arg && MatchesTime(expr.args.front(), visit.metadata.ctime, now, absl::Hours(24));
  if (name == "-cmin") return has_arg && MatchesTime(expr.args.front(), visit.metadata.ctime, now, absl::Minutes(1));
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
  if (name == "-printf") {
    if (has_arg) {
      emit(FormatPrintf(expr.args.front(), visit));  // no implicit newline; the format owns it
    }
    return true;
  }
  if (name == "-println") {
    emit(absl::StrCat(visit.path, kOsLineEnding));  // xff: -print with the OS line ending
    return true;
  }
  if (name == "-printfln") {
    if (has_arg) {  // xff: -printf plus the OS line ending appended
      emit(absl::StrCat(FormatPrintf(expr.args.front(), visit), kOsLineEnding));
    }
    return true;
  }
  if (name == "-delete") {
    static_cast<void>(fs.Remove(visit.path));  // failures set a nonzero exit; wired in the exit-code work
    return true;
  }
  if (name == "-exec") {
    if (!ctx.exec_fields) {
      return exec::Execute(expr.args, visit.path);  // find-exact: only {} is substituted (-> path)
    }
    // --exec-fields: render each token through the field vocabulary ({}, {name},
    // {path}, {root}, ...), then spawn the already-substituted argv.
    const fields::RenderContext render_ctx{
        .path = visit.path, .root = visit.root, .metadata = visit.metadata, .depth = visit.depth,
        .captures = ctx.captures, .defines = ctx.defines, .outputs = ctx.outputs};
    std::vector<std::string> argv;
    argv.reserve(expr.args.size());
    for (const std::string& token : expr.args) {
      argv.push_back(fields::Render(token, render_ctx));
    }
    return exec::ExecuteArgs(argv);  // true iff the child exits 0
  }
  if (name == "--capture") {  // args = [NAME, REGEX (may be empty), cmd...]
    if (ctx.outputs == nullptr || expr.args.size() < 3) {
      return true;  // unwired or malformed: binding is a no-op, but --capture is always true
    }
    // The command renders through the field vocabulary so {} -> path and prior
    // {capture.*}/{def.*}/{N} resolve (left-to-right chaining).
    const fields::RenderContext render_ctx{
        .path = visit.path, .root = visit.root, .metadata = visit.metadata, .depth = visit.depth,
        .captures = ctx.captures, .defines = ctx.defines, .outputs = ctx.outputs};
    std::vector<std::string> command;
    command.reserve(expr.args.size() - 2);
    for (std::size_t i = 2; i < expr.args.size(); ++i) {
      command.push_back(fields::Render(expr.args[i], render_ctx));
    }
    std::string value;
    if (const std::optional<std::string> out = exec::CaptureOutput(command); out.has_value()) {
      value = *out;
      while (!value.empty() && value.back() == '\n') {
        value.pop_back();  // strip trailing newline(s)
      }
      if (!expr.args[1].empty()) {
        value = ExtractCapture(expr.args[1], value);  // optional regex extraction
      }
    }
    (*ctx.outputs)[expr.args[0]] = std::move(value);  // bind {capture.NAME} (last wins)
    return true;  // a binding side effect; always true
  }
  if (name == "-prune") {
    control.prune = true;  // do not descend into this directory; -prune is always true
    return true;
  }
  if (name == "-quit") {
    control.quit = true;  // stop the whole traversal after this entry
    return true;
  }
  // Predicates not yet implemented evaluate to true (no-op); wired in follow-ups.
  return true;
}

}  // namespace

bool Evaluate(const parser::Expr& expr, EvalContext& context) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: return EvaluatePredicate(expr, context);
    case parser::Expr::Kind::kNot: return !Evaluate(*expr.lhs, context);
    case parser::Expr::Kind::kAnd: return Evaluate(*expr.lhs, context) && Evaluate(*expr.rhs, context);
    case parser::Expr::Kind::kOr: return Evaluate(*expr.lhs, context) || Evaluate(*expr.rhs, context);
    case parser::Expr::Kind::kComma:
      Evaluate(*expr.lhs, context);         // left operand: evaluated for side effects only
      return Evaluate(*expr.rhs, context);  // the list's value is the right operand's
  }
  return true;  // Unreachable: every Expr::Kind returns above.
}

bool ContainsAction(const parser::Expr& expr) {
  switch (expr.kind) {
    // -prune and --capture are actions that do NOT suppress the implicit print:
    // -prune per find's "no actions other than -prune" rule, and --capture is a
    // binding side effect (not output). -quit and the print actions do suppress.
    case parser::Expr::Kind::kPredicate:
      return expr.descriptor->kind == registry::Kind::kAction && expr.descriptor->name != "-prune" &&
             expr.descriptor->name != "--capture";
    case parser::Expr::Kind::kNot: return ContainsAction(*expr.lhs);
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kComma: return ContainsAction(*expr.lhs) || ContainsAction(*expr.rhs);
  }
  return false;  // Unreachable: every Expr::Kind returns above.
}

}  // namespace xff::engine
