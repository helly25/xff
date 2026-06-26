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
# define _GNU_SOURCE 1
#endif

#include "xff/engine/evaluate.h"

#include <fnmatch.h>
#include <grp.h>
#include <pwd.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "mbo/container/limited_map.h"
#include "xff/datetime/datetime.h"
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
  if (slash == std::string_view::npos) {
    return ".";
  }
  if (slash == 0) {
    return "/";
  }
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

// The 10-char symbolic permission string for -ls: a type char then user/group/
// other rwx triplets, with setuid/setgid/sticky shown as s/S/t/T (like ls).
std::string SymbolicPerms(vfs::FileType type, std::uint32_t mode) {
  std::string out(10, '-');
  switch (type) {
    case vfs::FileType::kRegular: out[0] = '-'; break;
    case vfs::FileType::kDirectory: out[0] = 'd'; break;
    case vfs::FileType::kSymlink: out[0] = 'l'; break;
    case vfs::FileType::kBlockDevice: out[0] = 'b'; break;
    case vfs::FileType::kCharDevice: out[0] = 'c'; break;
    case vfs::FileType::kFifo: out[0] = 'p'; break;
    case vfs::FileType::kSocket: out[0] = 's'; break;
    case vfs::FileType::kUnknown: out[0] = '?'; break;
  }
  static constexpr std::string_view kRwx = "rwx";
  for (int i = 0; i < 9; ++i) {
    if ((mode & (1U << (8 - i))) != 0U) {
      out[1 + i] = kRwx[i % 3];
    }
  }
  if ((mode & 04000U) != 0U) {  // setuid
    out[3] = out[3] == 'x' ? 's' : 'S';
  }
  if ((mode & 02000U) != 0U) {  // setgid
    out[6] = out[6] == 'x' ? 's' : 'S';
  }
  if ((mode & 01000U) != 0U) {  // sticky
    out[9] = out[9] == 'x' ? 't' : 'T';
  }
  return out;
}

// The -ls time column: "Mon DD HH:MM" for entries within ~6 months of `now`,
// else "Mon DD  YYYY" (like ls). Rendered in `tz`.
std::string LsTime(absl::Time mtime, absl::Time now, absl::TimeZone tz) {
  const bool recent = mtime > now - absl::Hours(24 * 182) && mtime < now + absl::Hours(1);
  return absl::FormatTime(recent ? "%b %e %H:%M" : "%b %e  %Y", mtime, tz);
}

// find's %u/%g: the owner/group name, or the numeric id when the user/group
// database has no entry for it (the reverse of ResolveUid/ResolveGid).
std::string UserName(std::uint32_t uid) {
  if (const struct passwd* const pw = ::getpwuid(uid); pw != nullptr) {
    return pw->pw_name;
  }
  return std::to_string(uid);
}

std::string GroupName(std::uint32_t gid) {
  if (const struct group* const gr = ::getgrgid(gid); gr != nullptr) {
    return gr->gr_name;
  }
  return std::to_string(gid);
}

// find's -printf \ escapes: the literal character each backslash sequence emits.
constexpr auto kPrintfEscapes = mbo::container::MakeLimitedMap(
    std::pair{'0', '\0'},
    std::pair{'\\', '\\'},
    std::pair{'n', '\n'},
    std::pair{'r', '\r'},
    std::pair{'t', '\t'});

// One -printf % directive: appends its expansion for `visit` to `out`. Captureless
// lambdas so the table below is a constexpr LimitedMap, like the engine's kDispatch.
using PrintfDirective = void (*)(std::string& out, const Visit& visit);

// find's -printf % directives, keyed by letter (alphabetical; '%' emits a literal %).
constexpr auto kPrintfDirectives = mbo::container::MakeLimitedMap(
    std::pair<char, PrintfDirective>{'%', [](std::string& out, const Visit&) { out.push_back('%'); }},
    std::pair<char, PrintfDirective>{
        'G', [](std::string& out, const Visit& v) { absl::StrAppend(&out, v.metadata.gid); }},
    std::pair<char, PrintfDirective>{
        'U', [](std::string& out, const Visit& v) { absl::StrAppend(&out, v.metadata.uid); }},
    std::pair<char, PrintfDirective>{'d', [](std::string& out, const Visit& v) { absl::StrAppend(&out, v.depth); }},
    std::pair<char, PrintfDirective>{'f', [](std::string& out, const Visit& v) { out.append(v.name); }},
    std::pair<char, PrintfDirective>{
        'g', [](std::string& out, const Visit& v) { out.append(GroupName(v.metadata.gid)); }},
    std::pair<char, PrintfDirective>{'h', [](std::string& out, const Visit& v) { out.append(Dirname(v.path)); }},
    std::pair<char, PrintfDirective>{
        'i', [](std::string& out, const Visit& v) { absl::StrAppend(&out, v.metadata.ino); }},
    std::pair<char, PrintfDirective>{
        'm', [](std::string& out, const Visit& v) { out.append(OctalPerm(v.metadata.mode)); }},
    std::pair<char, PrintfDirective>{
        'n', [](std::string& out, const Visit& v) { absl::StrAppend(&out, v.metadata.nlink); }},
    std::pair<char, PrintfDirective>{'p', [](std::string& out, const Visit& v) { out.append(v.path); }},
    std::pair<char, PrintfDirective>{
        's', [](std::string& out, const Visit& v) { absl::StrAppend(&out, v.metadata.size); }},
    std::pair<char, PrintfDirective>{
        'u', [](std::string& out, const Visit& v) { out.append(UserName(v.metadata.uid)); }},
    std::pair<char, PrintfDirective>{
        'y', [](std::string& out, const Visit& v) { out.push_back(TypeLetter(v.metadata.type)); }});

// The entry time a -printf time directive refers to: a/A -> atime, c/C -> ctime,
// t/T -> mtime (find's %a/%c/%t and the %Ak/%Ck/%Tk strftime families).
absl::Time PrintfTime(const vfs::Metadata& md, char which) {
  switch (which) {
    case 'A':
    case 'a': return md.atime;
    case 'C':
    case 'c': return md.ctime;
    default: return md.mtime;
  }
}

// find's -printf FORMAT: expands % directives and \ escapes against the entry via
// the tables above. Supported %: p path, f name, h dir, s size, m octal perm, d
// depth, y type, i inode, n links, u/g owner name, U/G owner id; the time families
// a/c/t (asctime form) and Ak/Ck/Tk (strftime conversion k on atime/ctime/mtime),
// rendered in `tz`; %% literal; \: n t r \\ \0. Unknown directives/escapes are
// emitted literally.
std::string FormatPrintf(std::string_view format, const Visit& visit, absl::TimeZone tz) {
  std::string out;
  for (std::string_view::size_type i = 0; i < format.size(); ++i) {
    const char ch = format[i];
    if (ch == '\\' && i + 1 < format.size()) {
      const char esc = format[++i];
      if (const auto it = kPrintfEscapes.find(esc); it != kPrintfEscapes.end()) {
        out.push_back(it->second);
      } else {
        out.push_back('\\');  // unknown escape: emit the backslash and char literally
        out.push_back(esc);
      }
    } else if (ch == '%' && i + 1 < format.size()) {
      const char directive = format[++i];
      if (directive == 'a' || directive == 'c' || directive == 't') {
        absl::StrAppend(&out, datetime::FormatTime(PrintfTime(visit.metadata, directive), "asctime", tz));
      } else if ((directive == 'A' || directive == 'C' || directive == 'T') && i + 1 < format.size()) {
        const char conv = format[++i];  // %Tk etc.: strftime conversion k on the chosen time
        absl::StrAppend(
            &out,
            datetime::FormatTime(PrintfTime(visit.metadata, directive), absl::StrCat("%", std::string(1, conv)), tz));
      } else if (const auto it = kPrintfDirectives.find(directive); it != kPrintfDirectives.end()) {
        it->second(out, visit);
      } else {
        out.push_back('%');  // unknown directive: emit the percent and char literally
        out.push_back(directive);
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
      case 'k': unit = 1'024; break;
      case 'M': unit = 1'024ULL * 1'024; break;
      case 'G': unit = 1'024ULL * 1'024 * 1'024; break;
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

// -samefile FILE: the entry is the same file as FILE, i.e. shares its inode AND
// device (so hard links to FILE match). FILE is stat'd following symlinks; a
// missing/unreadable reference makes it false. FILE is re-stat'd per entry for
// now, like IsNewerThan; resolving it once is a later optimization.
bool IsSameFile(const Visit& visit, std::string_view reference, const vfs::FileSystem& fs) {
  const absl::StatusOr<vfs::Metadata> ref = fs.Stat(reference, /*follow_symlinks=*/true);
  return ref.ok() && visit.metadata.ino == ref->ino && visit.metadata.dev == ref->dev;
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

// An optional reference to a node's pre-compiled matcher: empty when the node has
// no regex (no pattern, or it failed to compile). Explicit about the optionality,
// unlike a raw pointer.
using MatcherRef = std::optional<std::reference_wrapper<const regex::Matcher>>;

// Builds a MatcherRef from a node's Expr::matcher (a shared_ptr, possibly null).
MatcherRef AsRef(const std::shared_ptr<const regex::Matcher>& matcher) {
  if (matcher == nullptr) {
    return std::nullopt;
  }
  return std::cref(*matcher);
}

// find's -regex/-iregex: `matcher` (the node's pre-compiled Expr::matcher) must
// match the whole path (not a substring). An empty matcher (no pattern, or it
// failed to compile) matches nothing. When `captures` is non-null (gated -exec is
// active) a match records its groups there ([0] the whole match, 1..N the groups)
// for the {0}..{N} placeholders.
bool MatchesRegex(MatcherRef matcher, std::string_view path, std::vector<std::string>* captures) {
  if (!matcher.has_value()) {
    return false;
  }
  const regex::Matcher& re = matcher->get();
  if (captures == nullptr) {
    return re.FullMatch(path);
  }
  std::optional<std::vector<std::string>> groups = re.FullMatchCaptures(path);
  if (!groups.has_value()) {
    return false;
  }
  *captures = std::move(*groups);
  return true;
}

// Applies a -capture extraction matcher (Expr::matcher, pre-compiled from the
// optional =NAME=REGEX) to `text`: returns capture group 1, or the whole match
// when the regex has no groups, or empty when it does not fully match (or the
// matcher is empty -- no/uncompilable extraction regex).
std::string ExtractCapture(MatcherRef matcher, std::string_view text) {
  if (!matcher.has_value()) {
    return "";
  }
  const std::optional<std::vector<std::string>> groups = matcher->get().FullMatchCaptures(text);
  if (!groups.has_value()) {
    return "";
  }
  return groups->size() > 1 ? (*groups)[1] : (*groups)[0];
}

// --- Per-primary handlers. One free function per leaf test/action, each reading
// what it needs from `expr` and `ctx`. The dispatch table below maps a registry
// name to its handler, so evaluation is a constexpr-map lookup, not a linear
// name scan. Signature is uniform: (const Expr&, EvalContext&) -> bool. ---

bool EvalTrue(const parser::Expr&, EvalContext&) {
  return true;
}

bool EvalFalse(const parser::Expr&, EvalContext&) {
  return false;
}

// -name/-iname (and -path/-ipath below): the descriptor's fold_case selects
// FNM_CASEFOLD, so the case-insensitive variant is registry data, not a separate
// handler. -name and -iname both dispatch here.
bool EvalName(const parser::Expr& expr, EvalContext& ctx) {
  const int flags = expr.descriptor->fold_case ? FNM_CASEFOLD : 0;
  return !expr.args.empty() && Fnmatch(expr.args.front(), ctx.visit.name, flags);
}

bool EvalPath(const parser::Expr& expr, EvalContext& ctx) {
  const int flags = expr.descriptor->fold_case ? FNM_CASEFOLD : 0;
  return !expr.args.empty() && Fnmatch(expr.args.front(), ctx.visit.path, flags);
}

// Both -regex and -iregex map here: case sensitivity is baked into the matcher the
// parser compiled (iregex folds case), so the handler just matches the path.
bool EvalRegex(const parser::Expr& expr, EvalContext& ctx) {
  return MatchesRegex(AsRef(expr.matcher), ctx.visit.path, ctx.captures);
}

bool EvalType(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesType(expr.args.front(), ctx.visit.metadata.type);
}

bool EvalSize(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesSize(expr.args.front(), ctx.visit.metadata.size);
}

bool EvalLinks(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesNumeric(expr.args.front(), ctx.visit.metadata.nlink);
}

bool EvalInum(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesNumeric(expr.args.front(), ctx.visit.metadata.ino);
}

bool EvalUid(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesNumeric(expr.args.front(), ctx.visit.metadata.uid);
}

bool EvalGid(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesNumeric(expr.args.front(), ctx.visit.metadata.gid);
}

bool EvalUser(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const std::optional<std::uint32_t> uid = ResolveUid(expr.args.front());
  return uid.has_value() && ctx.visit.metadata.uid == *uid;
}

bool EvalGroup(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const std::optional<std::uint32_t> gid = ResolveGid(expr.args.front());
  return gid.has_value() && ctx.visit.metadata.gid == *gid;
}

bool EvalPerm(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesPerm(expr.args.front(), ctx.visit.metadata.mode);
}

bool EvalEmpty(const parser::Expr&, EvalContext& ctx) {
  return IsEmpty(ctx.visit, ctx.fs);
}

bool EvalNewer(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && IsNewerThan(ctx.visit, expr.args.front(), ctx.fs);
}

bool EvalSamefile(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && IsSameFile(ctx.visit, expr.args.front(), ctx.fs);
}

// -newerXY (X,Y in {a,c,m}); -newerXt (Y=t) compares the X-time to a time string.
// Shared by every 8-char -newer* name; reads X/Y from the descriptor.
bool EvalNewerXY(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const std::string_view name = expr.descriptor->name;
  const char x = name[6];
  if (name[7] == 't') {
    const std::optional<absl::Time> ref = datetime::ParseTimeString(expr.args.front(), ctx.now, ctx.tz);
    return ref.has_value() && TimeField(ctx.visit.metadata, x) > *ref;
  }
  return IsNewerXY(ctx.visit, x, name[7], expr.args.front(), ctx.fs);
}

bool EvalMtime(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesTime(expr.args.front(), ctx.visit.metadata.mtime, ctx.now, absl::Hours(24));
}

bool EvalMmin(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesTime(expr.args.front(), ctx.visit.metadata.mtime, ctx.now, absl::Minutes(1));
}

bool EvalAtime(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesTime(expr.args.front(), ctx.visit.metadata.atime, ctx.now, absl::Hours(24));
}

bool EvalAmin(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesTime(expr.args.front(), ctx.visit.metadata.atime, ctx.now, absl::Minutes(1));
}

bool EvalCtime(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesTime(expr.args.front(), ctx.visit.metadata.ctime, ctx.now, absl::Hours(24));
}

bool EvalCmin(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesTime(expr.args.front(), ctx.visit.metadata.ctime, ctx.now, absl::Minutes(1));
}

bool EvalPrint(const parser::Expr&, EvalContext& ctx) {
  ctx.emit(absl::StrCat(ctx.visit.path, "\n"));
  return true;
}

bool EvalPrint0(const parser::Expr&, EvalContext& ctx) {
  std::string record(ctx.visit.path);
  record.push_back('\0');
  ctx.emit(record);
  return true;
}

// find's -ls: an `ls -dils`-style line per entry -- inode, 1 KiB blocks, symbolic
// permissions, link count, owner, group, size, time, and path -- rendered in
// ctx.tz. Columns are single-space-separated (xff does not pad to find's widths).
bool EvalLs(const parser::Expr&, EvalContext& ctx) {
  const vfs::Metadata& md = ctx.visit.metadata;
  const std::uint64_t blocks_1k = (md.blocks + 1) / 2;  // 512-byte blocks -> 1 KiB blocks (rounded up)
  ctx.emit(
      absl::StrCat(
          md.ino, " ", blocks_1k, " ", SymbolicPerms(md.type, md.mode), " ", md.nlink, " ", UserName(md.uid), " ",
          GroupName(md.gid), " ", md.size, " ", LsTime(md.mtime, ctx.now, ctx.tz), " ", ctx.visit.path, "\n"));
  return true;
}

bool EvalPrintf(const parser::Expr& expr, EvalContext& ctx) {
  if (!expr.args.empty()) {
    ctx.emit(FormatPrintf(expr.args.front(), ctx.visit, ctx.tz));  // no implicit newline; the format owns it
  }
  return true;
}

bool EvalPrintln(const parser::Expr&, EvalContext& ctx) {
  ctx.emit(absl::StrCat(ctx.visit.path, kOsLineEnding));  // xff: -print with the OS line ending
  return true;
}

bool EvalPrintfln(const parser::Expr& expr, EvalContext& ctx) {
  if (!expr.args.empty()) {  // xff: -printf plus the OS line ending appended
    ctx.emit(absl::StrCat(FormatPrintf(expr.args.front(), ctx.visit, ctx.tz), kOsLineEnding));
  }
  return true;
}

bool EvalDelete(const parser::Expr&, EvalContext& ctx) {
  static_cast<void>(ctx.fs.Remove(ctx.visit.path));  // failures set a nonzero exit; wired in the exit-code work
  return true;
}

bool EvalExec(const parser::Expr& expr, EvalContext& ctx) {
  if (!ctx.exec_fields) {
    return exec::Execute(expr.args, ctx.visit.path);  // find-exact: only {} is substituted (-> path)
  }
  // --exec-fields: render each token through the field vocabulary ({}, {name},
  // {path}, {root}, ...), then spawn the already-substituted argv.
  const fields::RenderContext render_ctx{
      .path = ctx.visit.path,
      .root = ctx.visit.root,
      .metadata = ctx.visit.metadata,
      .depth = ctx.visit.depth,
      .tz = ctx.tz,
      .time_format = ctx.time_format,
      .captures = ctx.captures,
      .defines = ctx.defines,
      .outputs = ctx.outputs};
  std::vector<std::string> argv;
  argv.reserve(expr.args.size());
  for (const std::string& token : expr.args) {
    argv.push_back(fields::Render(token, render_ctx));
  }
  return exec::ExecuteArgs(argv);  // true iff the child exits 0
}

// Splits an entry path into the directory that -execdir/-okdir run the child in
// and the "./<basename>" that find substitutes for {}. A top-level entry (no
// slash) runs in "."; an entry at the root ("/x") runs in "/".
struct ExecDir {
  std::string dir;
  std::string brace;  // "./<basename>"
};

ExecDir SplitExecDir(std::string_view path) {
  const auto slash = path.find_last_of('/');
  if (slash == std::string_view::npos) {
    return {.dir = ".", .brace = absl::StrCat("./", path)};  // top-level entry: run in the current directory
  }
  return {
      .dir = (slash == 0) ? "/" : std::string(path.substr(0, slash)),
      .brace = absl::StrCat("./", path.substr(slash + 1))};
}

// Builds the -ok/-okdir confirmation prompt: each command token with "{}" replaced
// by `subst`, space-joined, then "? " (find's interactive form).
std::string OkPrompt(const std::vector<std::string>& args, std::string_view subst) {
  std::string prompt;
  for (const std::string& token : args) {
    if (!prompt.empty()) {
      prompt += ' ';
    }
    std::string rendered = token;
    for (std::size_t pos = 0; (pos = rendered.find("{}", pos)) != std::string::npos; pos += subst.size()) {
      rendered.replace(pos, 2, std::string(subst));
    }
    prompt += rendered;
  }
  prompt += "? ";
  return prompt;
}

bool EvalExecdir(const parser::Expr& expr, EvalContext& ctx) {
  // Like -exec, but the child runs with its working directory set to the directory
  // containing the matched entry, and find-exact {} expands to "./<basename>".
  const ExecDir target = SplitExecDir(ctx.visit.path);
  if (!ctx.exec_fields) {
    return exec::ExecuteInDir(expr.args, target.dir, target.brace);  // find-exact: {} -> ./basename
  }
  // --exec-fields: render each token through the field vocabulary (which still
  // sees the full path/root), then spawn in the entry's directory.
  const fields::RenderContext render_ctx{
      .path = ctx.visit.path,
      .root = ctx.visit.root,
      .metadata = ctx.visit.metadata,
      .depth = ctx.visit.depth,
      .tz = ctx.tz,
      .time_format = ctx.time_format,
      .captures = ctx.captures,
      .defines = ctx.defines,
      .outputs = ctx.outputs};
  std::vector<std::string> argv;
  argv.reserve(expr.args.size());
  for (const std::string& token : expr.args) {
    argv.push_back(fields::Render(token, render_ctx));
  }
  return exec::ExecuteArgsInDir(argv, target.dir);
}

bool EvalOk(const parser::Expr& expr, EvalContext& ctx) {
  // Like -exec, but prompt on stderr (find-exact: {} -> path) and run only on an
  // affirmative reply. Declined, or no confirmer wired -> false, per find.
  if (!ctx.confirm || !ctx.confirm(OkPrompt(expr.args, ctx.visit.path))) {
    return false;
  }
  return exec::Execute(expr.args, ctx.visit.path);  // substitutes {} -> path; true iff the child exits 0
}

bool EvalOkdir(const parser::Expr& expr, EvalContext& ctx) {
  // -okdir is to -execdir what -ok is to -exec: prompt (showing {} -> ./basename),
  // then on an affirmative reply run the command in the matched entry's directory.
  if (!ctx.confirm) {
    return false;
  }
  const ExecDir target = SplitExecDir(ctx.visit.path);
  if (!ctx.confirm(OkPrompt(expr.args, target.brace))) {
    return false;  // declined -> not run; -okdir is false
  }
  return exec::ExecuteInDir(expr.args, target.dir, target.brace);
}

// Shared body of -capture/-capturedir: render the command through the field
// vocabulary, run it (in `dir`, or our directory when `dir` is empty/"."), capture
// stdout, strip trailing newlines, optionally regex-extract, and bind {capture.NAME}.
bool RunCapture(const parser::Expr& expr, EvalContext& ctx, std::string_view dir) {  // args = [NAME, REGEX, cmd...]
  if (ctx.outputs == nullptr || expr.args.size() < 3) {
    return true;  // unwired or malformed: binding is a no-op, but -capture is always true
  }
  // The command renders through the field vocabulary so {} -> path and prior
  // {capture.*}/{def.*}/{N} resolve (left-to-right chaining).
  const fields::RenderContext render_ctx{
      .path = ctx.visit.path,
      .root = ctx.visit.root,
      .metadata = ctx.visit.metadata,
      .depth = ctx.visit.depth,
      .tz = ctx.tz,
      .time_format = ctx.time_format,
      .captures = ctx.captures,
      .defines = ctx.defines,
      .outputs = ctx.outputs};
  std::vector<std::string> command;
  command.reserve(expr.args.size() - 2);
  for (std::size_t i = 2; i < expr.args.size(); ++i) {
    command.push_back(fields::Render(expr.args[i], render_ctx));
  }
  std::string value;
  if (const std::optional<std::string> out = exec::CaptureOutput(command, dir); out.has_value()) {
    value = *out;
    while (!value.empty() && value.back() == '\n') {
      value.pop_back();  // strip trailing newline(s)
    }
    if (!expr.args[1].empty()) {
      value = ExtractCapture(AsRef(expr.matcher), value);  // optional regex extraction (matcher pre-compiled)
    }
  }
  (*ctx.outputs)[expr.args[0]] = std::move(value);  // bind {capture.NAME} (last wins)
  return true;                                      // a binding side effect; always true
}

bool EvalCapture(const parser::Expr& expr, EvalContext& ctx) {
  return RunCapture(expr, ctx, /*dir=*/{});
}

// -capturedir: -capture run in the matched entry's directory (the -execdir of -capture).
bool EvalCapturedir(const parser::Expr& expr, EvalContext& ctx) {
  return RunCapture(expr, ctx, Dirname(ctx.visit.path));
}

bool EvalPrune(const parser::Expr&, EvalContext& ctx) {
  ctx.control.prune = true;  // do not descend into this directory; -prune is always true
  return true;
}

bool EvalQuit(const parser::Expr&, EvalContext& ctx) {
  ctx.control.quit = true;  // stop the whole traversal after this entry
  return true;
}

// Engine-side dispatch entry for one primary. A struct (not a bare function
// pointer) so per-primary engine config can be added here as it is designed,
// without changing the table or its call site. Declarative metadata (kind,
// arity, and the coming mode/feature/safety classification) lives on the
// registry Descriptor (the SOT, below the parser); the eval function is the one
// piece that must be engine-side, so it lives here and is keyed by the same
// name. A registry/dispatch consistency test guards the pairing.
using EvalFn = bool (*)(const parser::Expr&, EvalContext&);

struct EvalEntry {
  EvalFn eval = nullptr;
};

using DispatchPair = std::pair<std::string_view, EvalEntry>;
constexpr auto kDispatch = mbo::container::MakeLimitedMap(
    DispatchPair{"-amin", {&EvalAmin}},
    DispatchPair{"-atime", {&EvalAtime}},
    DispatchPair{"-capture", {&EvalCapture}},
    DispatchPair{"-capturedir", {&EvalCapturedir}},
    DispatchPair{"-cmin", {&EvalCmin}},
    DispatchPair{"-ctime", {&EvalCtime}},
    DispatchPair{"-delete", {&EvalDelete}},
    DispatchPair{"-empty", {&EvalEmpty}},
    DispatchPair{"-exec", {&EvalExec}},
    DispatchPair{"-execdir", {&EvalExecdir}},
    DispatchPair{"-false", {&EvalFalse}},
    DispatchPair{"-gid", {&EvalGid}},
    DispatchPair{"-group", {&EvalGroup}},
    DispatchPair{"-iname", {&EvalName}},
    DispatchPair{"-inum", {&EvalInum}},
    DispatchPair{"-ipath", {&EvalPath}},
    DispatchPair{"-iregex", {&EvalRegex}},
    DispatchPair{"-links", {&EvalLinks}},
    DispatchPair{"-ls", {&EvalLs}},
    DispatchPair{"-mmin", {&EvalMmin}},
    DispatchPair{"-mtime", {&EvalMtime}},
    DispatchPair{"-name", {&EvalName}},
    DispatchPair{"-newer", {&EvalNewer}},
    DispatchPair{"-neweraa", {&EvalNewerXY}},
    DispatchPair{"-newerac", {&EvalNewerXY}},
    DispatchPair{"-neweram", {&EvalNewerXY}},
    DispatchPair{"-newerat", {&EvalNewerXY}},
    DispatchPair{"-newerca", {&EvalNewerXY}},
    DispatchPair{"-newercc", {&EvalNewerXY}},
    DispatchPair{"-newercm", {&EvalNewerXY}},
    DispatchPair{"-newerct", {&EvalNewerXY}},
    DispatchPair{"-newerma", {&EvalNewerXY}},
    DispatchPair{"-newermc", {&EvalNewerXY}},
    DispatchPair{"-newermm", {&EvalNewerXY}},
    DispatchPair{"-newermt", {&EvalNewerXY}},
    DispatchPair{"-ok", {&EvalOk}},
    DispatchPair{"-okdir", {&EvalOkdir}},
    DispatchPair{"-path", {&EvalPath}},
    DispatchPair{"-perm", {&EvalPerm}},
    DispatchPair{"-print", {&EvalPrint}},
    DispatchPair{"-print0", {&EvalPrint0}},
    DispatchPair{"-printf", {&EvalPrintf}},
    DispatchPair{"-printfln", {&EvalPrintfln}},
    DispatchPair{"-println", {&EvalPrintln}},
    DispatchPair{"-prune", {&EvalPrune}},
    DispatchPair{"-quit", {&EvalQuit}},
    DispatchPair{"-regex", {&EvalRegex}},
    DispatchPair{"-samefile", {&EvalSamefile}},
    DispatchPair{"-size", {&EvalSize}},
    DispatchPair{"-true", {&EvalTrue}},
    DispatchPair{"-type", {&EvalType}},
    DispatchPair{"-uid", {&EvalUid}},
    DispatchPair{"-user", {&EvalUser}});

bool EvaluatePredicate(const parser::Expr& expr, EvalContext& ctx) {
  // O(log n) dispatch on the descriptor name. A name not in the table (e.g. a
  // traversal option like -maxdepth, consumed by the walk, not per entry) is a
  // no-op that evaluates true, matching the previous fall-through.
  const auto it = kDispatch.find(expr.descriptor->name);
  return it == kDispatch.end() || it->second.eval(expr, ctx);
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
    // -prune and -capture are actions that do NOT suppress the implicit print:
    // -prune per find's "no actions other than -prune" rule, and -capture is a
    // binding side effect (not output). -quit and the print actions do suppress.
    case parser::Expr::Kind::kPredicate:
      return expr.descriptor->kind == registry::Kind::kAction && expr.descriptor->name != "-prune"
             && expr.descriptor->name != "-capture";
    case parser::Expr::Kind::kNot: return ContainsAction(*expr.lhs);
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kComma: return ContainsAction(*expr.lhs) || ContainsAction(*expr.rhs);
  }
  return false;  // Unreachable: every Expr::Kind returns above.
}

}  // namespace xff::engine
