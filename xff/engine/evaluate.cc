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
#include "mbo/container/limited_set.h"
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

// The (letter, type) pairs find's -type/-xtype letters denote, alphabetical by
// letter: matching an entry is then one membership test (contains).
constexpr auto kTypeChars = mbo::container::MakeLimitedSet(
    std::pair{'b', vfs::FileType::kBlockDevice},
    std::pair{'c', vfs::FileType::kCharDevice},
    std::pair{'d', vfs::FileType::kDirectory},
    std::pair{'f', vfs::FileType::kRegular},
    std::pair{'l', vfs::FileType::kSymlink},
    std::pair{'p', vfs::FileType::kFifo},
    std::pair{'s', vfs::FileType::kSocket});

bool MatchesTypeChar(char letter, vfs::FileType type) {
  return kTypeChars.contains(std::pair{letter, type});
}

// A valid -type/-xtype letter (the keys of kTypeChars), to reject an unknown one.
bool IsTypeChar(char letter) {
  return std::string_view("bcdflps").contains(letter);
}

// find's -type / -xtype argument: a single type letter, or (GNU extension) a
// comma-separated list like "f,d" that matches if the entry is any listed type.
// An empty, multi-character, or unknown element fails the whole match.
bool MatchesType(std::string_view arg, vfs::FileType type) {
  if (arg.empty()) {
    return false;
  }
  bool matched = false;
  while (true) {
    const std::size_t comma = arg.find(',');
    const std::string_view item = arg.substr(0, comma);
    if (item.size() != 1 || !IsTypeChar(item.front())) {
      return false;
    }
    matched = matched || MatchesTypeChar(item.front(), type);
    if (comma == std::string_view::npos) {
      break;
    }
    arg.remove_prefix(comma + 1);
  }
  return matched;
}

// find's %y type letter: the inverse of MatchesType's mapping.
char TypeLetter(vfs::FileType type) {
  switch (type) {
    case vfs::FileType::kBlockDevice: return 'b';
    case vfs::FileType::kCharDevice: return 'c';
    case vfs::FileType::kDirectory: return 'd';
    case vfs::FileType::kFifo: return 'p';
    case vfs::FileType::kRegular: return 'f';
    case vfs::FileType::kSocket: return 's';
    case vfs::FileType::kSymlink: return 'l';
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
    case vfs::FileType::kBlockDevice: out[0] = 'b'; break;
    case vfs::FileType::kCharDevice: out[0] = 'c'; break;
    case vfs::FileType::kDirectory: out[0] = 'd'; break;
    case vfs::FileType::kFifo: out[0] = 'p'; break;
    case vfs::FileType::kRegular: out[0] = '-'; break;
    case vfs::FileType::kSocket: out[0] = 's'; break;
    case vfs::FileType::kSymlink: out[0] = 'l'; break;
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

// find's `-size` unit suffixes -> bytes per unit (c=char/1, w=2-byte word, b=512
// block; k/M/G/T/P/E are the binary multiples 2^10..2^60). b/c/w/k/M/G and T/P are
// find-native (BSD accepts up to P); E (exabyte) is an xff continuation of the same
// scale -- a strict superset (no find-valid input changes meaning), so it is
// available in every style. The next prefixes (Z/Y/...) name a real magnitude but
// 2^70+ overflows the 64-bit byte count, so they are rejected (see ParseSizeSpec).
// A constexpr map, per the style's preference for a uniform key -> value mapping.
using SizeUnitPair = std::pair<char, std::uint64_t>;
constexpr auto kSizeUnits = mbo::container::MakeLimitedMap(
    SizeUnitPair{'E', 1'024ULL * 1'024 * 1'024 * 1'024 * 1'024 * 1'024},  // 2^60 exbibyte
    SizeUnitPair{'G', 1'024ULL * 1'024 * 1'024},                          // 2^30 gibibyte
    SizeUnitPair{'M', 1'024ULL * 1'024},                                  // 2^20 mebibyte
    SizeUnitPair{'P', 1'024ULL * 1'024 * 1'024 * 1'024 * 1'024},          // 2^50 pebibyte
    SizeUnitPair{'T', 1'024ULL * 1'024 * 1'024 * 1'024},                  // 2^40 tebibyte
    SizeUnitPair{'b', 512},
    SizeUnitPair{'c', 1},
    SizeUnitPair{'k', 1'024},
    SizeUnitPair{'w', 2});

// Size prefixes one step beyond E: each names a real magnitude (zetta/yotta/ronna/
// quetta) but 2^70+ exceeds the 64-bit byte count, so xff rejects them with a clear
// message rather than silently mis-sizing.
constexpr std::string_view kOversizedUnits = "ZYRQ";

struct SizeSpec {
  char compare = '=';        // '+' greater than, '-' less than, '=' exactly
  std::uint64_t want = 0;    // the count, in `unit`s
  std::uint64_t unit = 512;  // bytes per unit (find default: 512-byte blocks)
};

// Parses a `-size` argument `[+|-]N[unit]` into a SizeSpec, or returns an
// InvalidArgument status naming the problem (unknown unit, an over-64-bit unit, or
// a missing/non-numeric count). Used to reject a bad value before the walk and,
// defensively, by MatchesSize.
absl::StatusOr<SizeSpec> ParseSizeSpec(std::string_view arg) {
  const std::string_view original = arg;
  SizeSpec spec;
  if (!arg.empty() && (arg.front() == '+' || arg.front() == '-')) {
    spec.compare = arg.front();
    arg.remove_prefix(1);
  }
  if (!arg.empty() && (arg.back() < '0' || arg.back() > '9')) {
    const char suffix = arg.back();
    const auto it = kSizeUnits.find(suffix);
    if (it == kSizeUnits.end()) {
      if (kOversizedUnits.find(suffix) != std::string_view::npos) {
        return absl::InvalidArgumentError(
            absl::StrCat(
                "'", original, "': size unit '", std::string(1, suffix),
                "' exceeds xff's 64-bit byte range; the largest size unit is E (exabyte)"));
      }
      return absl::InvalidArgumentError(
          absl::StrCat("'", original, "': unknown size unit '", std::string(1, suffix), "'"));
    }
    spec.unit = it->second;
    arg.remove_suffix(1);
  }
  if (arg.empty()) {
    return absl::InvalidArgumentError(absl::StrCat("'", original, "': missing numeric size"));
  }
  for (const char digit : arg) {
    if (digit < '0' || digit > '9') {
      return absl::InvalidArgumentError(absl::StrCat("'", original, "': size is not a number"));
    }
    spec.want = (spec.want * 10) + static_cast<std::uint64_t>(digit - '0');
  }
  return spec;
}

// Matches find's `-size N[bcwkMGTPE]` with an optional +/- prefix. The file size is
// rounded UP to the chosen unit (default 512-byte blocks), as find does. A
// malformed arg never matches (it is rejected before the walk; see ValidateSizeArgs).
bool MatchesSize(std::string_view arg, std::uint64_t size_bytes) {
  const absl::StatusOr<SizeSpec> spec = ParseSizeSpec(arg);
  if (!spec.ok()) {
    return false;
  }
  const std::uint64_t size_in_units = (size_bytes + spec->unit - 1) / spec->unit;
  if (spec->compare == '+') {
    return size_in_units > spec->want;
  }
  if (spec->compare == '-') {
    return size_in_units < spec->want;
  }
  return size_in_units == spec->want;
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

// Like MatchesNumeric, but over a signed count: find's -used day delta is
// negative when a file's access time predates its status-change time. The
// argument N is still non-negative (a leading +/- is the comparison operator).
bool MatchesSignedNumeric(std::string_view arg, std::int64_t value) {
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
    want = want * 10 + static_cast<std::int64_t>(digit - '0');
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
// Resolves one chmod-style symbolic clause ("u+w", "go=r", "+x", "u+s", ...)
// into `want`, applied from find's zero base with no umask. Returns false on a
// syntax error. 'X' is treated as 'x' (find resolves -perm with no per-file
// context, so the conditional-execute form degenerates to plain execute).
bool ApplyPermClause(std::string_view clause, std::uint32_t* want) {
  bool user = false;
  bool group = false;
  bool other = false;
  std::size_t i = 0;
  for (; i < clause.size(); ++i) {
    const char who = clause[i];
    if (who == 'u') {
      user = true;
    } else if (who == 'g') {
      group = true;
    } else if (who == 'o') {
      other = true;
    } else if (who == 'a') {
      user = group = other = true;
    } else {
      break;
    }
  }
  if (!user && !group && !other) {
    user = group = other = true;  // an omitted "who" behaves as 'a' (find applies no umask)
  }
  if (i >= clause.size()) {
    return false;  // missing operator
  }
  const char op = clause[i++];
  if (op != '+' && op != '-' && op != '=') {
    return false;
  }
  bool read = false;
  bool write = false;
  bool exec = false;
  bool setid = false;
  bool sticky = false;
  for (; i < clause.size(); ++i) {
    switch (clause[i]) {
      case 'X':
      case 'x': exec = true; break;
      case 'r': read = true; break;
      case 's': setid = true; break;
      case 't': sticky = true; break;
      case 'w': write = true; break;
      default: return false;
    }
  }
  std::uint32_t pattern = 0;
  std::uint32_t who_mask = 0;
  if (user) {
    pattern |= (read ? 0400U : 0U) | (write ? 0200U : 0U) | (exec ? 0100U : 0U) | (setid ? 04000U : 0U);
    who_mask |= 04700U;
  }
  if (group) {
    pattern |= (read ? 0040U : 0U) | (write ? 0020U : 0U) | (exec ? 0010U : 0U) | (setid ? 02000U : 0U);
    who_mask |= 02070U;
  }
  if (other) {
    pattern |= (read ? 0004U : 0U) | (write ? 0002U : 0U) | (exec ? 0001U : 0U);
    who_mask |= 01007U;
  }
  if (sticky) {
    pattern |= 01000U;
  }
  if (op == '+') {
    *want |= pattern;
  } else if (op == '-') {
    *want &= ~pattern;
  } else {
    *want = (*want & ~who_mask) | pattern;  // '=' clears the affected classes first
  }
  return true;
}

// Parses a full symbolic mode (comma-separated clauses) from a zero base, or
// nullopt on any syntax error.
std::optional<std::uint32_t> ParseSymbolicPerm(std::string_view spec) {
  if (spec.empty()) {
    return std::nullopt;
  }
  std::uint32_t want = 0;
  while (true) {
    const std::size_t comma = spec.find(',');
    if (!ApplyPermClause(spec.substr(0, comma), &want)) {
      return std::nullopt;
    }
    if (comma == std::string_view::npos) {
      break;
    }
    spec.remove_prefix(comma + 1);
  }
  return want;
}

bool MatchesPerm(std::string_view arg, std::uint32_t mode) {
  char op = '=';
  if (!arg.empty() && (arg.front() == '-' || arg.front() == '/')) {
    op = arg.front();  // '-' all-of, '/' (GNU) any-of, bare exact
    arg.remove_prefix(1);
  } else if (arg.size() > 1 && arg.front() == '+' && arg.find_first_not_of("01234567", 1) == std::string_view::npos) {
    op = '+';  // BSD '+octal' is any-of (like '/'); a symbolic "+r" stays an exact mode (== 0444)
    arg.remove_prefix(1);
  }
  if (arg.empty()) {
    return false;
  }
  std::uint32_t want = 0;
  if (arg.find_first_not_of("01234567") == std::string_view::npos) {
    for (const char digit : arg) {
      want = want * 8 + static_cast<std::uint32_t>(digit - '0');
    }
  } else {
    const std::optional<std::uint32_t> symbolic = ParseSymbolicPerm(arg);
    if (!symbolic.has_value()) {
      return false;  // neither octal nor a valid symbolic mode
    }
    want = *symbolic;
  }
  const std::uint32_t bits = mode & 07777U;  // permission + setuid/setgid/sticky bits
  if (op == '-') {
    return (bits & want) == want;  // all requested bits set
  }
  if (op == '/' || op == '+') {
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

// Selects a timestamp by find's X/Y letter: a=access, c=inode-change, m=modify,
// B=birth. Birth time is optional (only some kernels/filesystems record it), so
// 'B' may yield no value; a/c/m are always present (returned as an engaged option).
std::optional<absl::Time> TimeField(const vfs::Metadata& metadata, char field) {
  switch (field) {
    case 'a': return metadata.atime;
    case 'c': return metadata.ctime;
    case 'B': return metadata.btime;  // empty when birthtime is unrecorded
    default: return metadata.mtime;   // 'm'
  }
}

// find's -newerXY (X,Y in {a,B,c,m}): the entry's X-time is more recent than the
// reference FILE's Y-time. The reference is stat'd following symlinks; a missing
// reference makes it false, as does an unrecorded birth time on either side (X=B
// or Y=B). (The Y=t time-string form is handled in EvalNewerXY.)
bool IsNewerXY(const Visit& visit, char x, char y, std::string_view reference, const vfs::FileSystem& fs) {
  const absl::StatusOr<vfs::Metadata> ref = fs.Stat(reference, /*follow_symlinks=*/true);
  if (!ref.ok()) {
    return false;
  }
  const std::optional<absl::Time> lhs = TimeField(visit.metadata, x);
  const std::optional<absl::Time> rhs = TimeField(*ref, y);
  return lhs.has_value() && rhs.has_value() && *lhs > *rhs;
}

// BSD's `-mtime`/`-atime`/`-ctime` trailing unit suffix -> the duration of one
// unit. A constexpr map, per the style's preference for a uniform key -> value
// mapping over a switch.
using TimeUnitPair = std::pair<char, absl::Duration>;
constexpr auto kTimeUnits = mbo::container::MakeLimitedMap(
    TimeUnitPair{'d', absl::Hours(24)},
    TimeUnitPair{'h', absl::Hours(1)},
    TimeUnitPair{'m', absl::Minutes(1)},
    TimeUnitPair{'s', absl::Seconds(1)},
    TimeUnitPair{'w', absl::Hours(24 * 7)});

// find's -mtime/-mmin: the entry was modified N units ago -- 24h for -mtime, one
// minute for -mmin -- with any fractional unit discarded (floor), so a 2.9-day
// file is "2 days". +N means strictly more than N units ago, -N strictly fewer.
bool MatchesTime(std::string_view arg, absl::Time mtime, absl::Time now, absl::Duration unit, bool allow_unit_suffix) {
  char compare = '=';
  if (!arg.empty() && (arg.front() == '+' || arg.front() == '-')) {
    compare = arg.front();
    arg.remove_prefix(1);
  }
  if (arg.empty()) {
    return false;
  }
  // BSD unit suffix: -mtime/-atime/-ctime accept a trailing s/m/h/d/w that
  // overrides the predicate's default unit (e.g. "-mtime -1h"). find-compatible
  // (BSD); the GNU -mmin/-amin/-cmin family keeps integer minutes (no suffix).
  if (allow_unit_suffix && (arg.back() < '0' || arg.back() > '9')) {
    const auto it = kTimeUnits.find(arg.back());
    if (it == kTimeUnits.end()) {
      return false;  // unrecognised suffix
    }
    unit = it->second;
    arg.remove_suffix(1);
    if (arg.empty()) {
      return false;
    }
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

// xff word/compound duration on -mtime/-atime/-ctime (e.g. "-3 weeks 3 hours",
// "+2 days"): '+' = older than the span, '-' = younger than it. The span reuses
// ParseTimeString ("<span> ago" is the instant the span reaches back to), so the
// full relative grammar (compound terms, calendar units) carries over. The form
// requires an explicit sign; an unparseable span never matches. This is the lone
// xff-only time form (gated out of --config=find); see docs/design-find-flavors.md.
bool MatchesAge(std::string_view arg, absl::Time time, absl::Time now, absl::TimeZone tz) {
  if (arg.empty() || (arg.front() != '+' && arg.front() != '-')) {
    return false;
  }
  const char compare = arg.front();
  arg.remove_prefix(1);
  const std::optional<absl::Time> ref = datetime::ParseTimeString(absl::StrCat(arg, " ago"), now, tz);
  if (!ref.has_value()) {
    return false;
  }
  return compare == '+' ? time < *ref : time > *ref;
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

// -lname/-ilname: glob the symlink's *target* text (the link is never resolved).
// Only a symlink can match; the descriptor's fold_case selects -ilname's
// FNM_CASEFOLD, mirroring -name/-iname.
bool EvalLname(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty() || ctx.visit.metadata.type != vfs::FileType::kSymlink) {
    return false;
  }
  const absl::StatusOr<std::string> target = ctx.fs.ReadLink(ctx.visit.path);
  if (!target.ok()) {
    return false;
  }
  const int flags = expr.descriptor->fold_case ? FNM_CASEFOLD : 0;
  return Fnmatch(expr.args.front(), *target, flags);
}

// Both -regex and -iregex map here: case sensitivity is baked into the matcher the
// parser compiled (iregex folds case), so the handler just matches the path.
bool EvalRegex(const parser::Expr& expr, EvalContext& ctx) {
  return MatchesRegex(AsRef(expr.matcher), ctx.visit.path, ctx.captures);
}

bool EvalType(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesType(expr.args.front(), ctx.visit.metadata.type);
}

// -xtype: like -type, but for a symlink it tests the type of the link's *target*
// (the link is followed). A broken symlink is reported as a symlink, so
// "-xtype l" matches it, matching GNU find under the default -P.
bool EvalXtype(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  if (ctx.visit.metadata.type != vfs::FileType::kSymlink) {
    return MatchesType(expr.args.front(), ctx.visit.metadata.type);
  }
  const absl::StatusOr<vfs::Metadata> target = ctx.fs.Stat(ctx.visit.path, /*follow_symlinks=*/true);
  const vfs::FileType type = target.ok() ? target->type : vfs::FileType::kSymlink;
  return MatchesType(expr.args.front(), type);
}

// -fstype: matches when the filesystem holding the entry has the given type
// name (e.g. "ext2/ext3", "apfs", "tmpfs", "nfs"). The recognised names are
// platform-specific -- macOS/BSD report `f_fstypename` verbatim, Linux maps the
// `statfs` magic to a find-compatible name -- so a portable expression usually
// tests a single known value. An unqueryable path never matches.
bool EvalFstype(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const absl::StatusOr<std::string> type = ctx.fs.FsType(ctx.visit.path);
  return type.ok() && *type == expr.args.front();
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

// find's -used N: the entry was last accessed N days after its status last
// changed, i.e. trunc((atime - ctime) / day). The delta is negative when the
// access predates the status change; N uses the usual N / +N / -N comparison.
bool EvalUsed(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const std::int64_t seconds = absl::ToInt64Seconds(ctx.visit.metadata.atime - ctx.visit.metadata.ctime);
  return MatchesSignedNumeric(expr.args.front(), seconds / 86'400);
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

// find's -nouser / -nogroup: the entry's owner uid / group gid has no entry in
// the passwd / group database (an orphaned id).
bool EvalNouser(const parser::Expr&, EvalContext& ctx) {
  return ::getpwuid(ctx.visit.metadata.uid) == nullptr;
}

bool EvalNogroup(const parser::Expr&, EvalContext& ctx) {
  return ::getgrgid(ctx.visit.metadata.gid) == nullptr;
}

bool EvalPerm(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesPerm(expr.args.front(), ctx.visit.metadata.mode);
}

bool EvalEmpty(const parser::Expr&, EvalContext& ctx) {
  return IsEmpty(ctx.visit, ctx.fs);
}

// find's -sparse: the file has holes -- fewer 512-byte blocks allocated than its
// apparent size needs (st_blocks * 512 < st_size). A zero-size file is not sparse.
bool EvalSparse(const parser::Expr&, EvalContext& ctx) {
  const vfs::Metadata& md = ctx.visit.metadata;
  return md.size > 0 && md.blocks * 512U < md.size;
}

// find's -readable/-writable/-executable: the current user can access the entry
// for that mode (a real access() probe, not just a mode-bit guess).
bool EvalReadable(const parser::Expr&, EvalContext& ctx) {
  return ctx.fs.Access(ctx.visit.path, vfs::AccessMode::kRead);
}

bool EvalWritable(const parser::Expr&, EvalContext& ctx) {
  return ctx.fs.Access(ctx.visit.path, vfs::AccessMode::kWrite);
}

bool EvalExecutable(const parser::Expr&, EvalContext& ctx) {
  return ctx.fs.Access(ctx.visit.path, vfs::AccessMode::kExecute);
}

bool EvalNewer(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && IsNewerThan(ctx.visit, expr.args.front(), ctx.fs);
}

bool EvalSamefile(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && IsSameFile(ctx.visit, expr.args.front(), ctx.fs);
}

// A birth-time predicate hit an entry whose filesystem/kernel did not record a
// birth time: it cannot be evaluated correctly here (an impossible task). Flags the
// entry on the control side-channel for the driver (hard error, or warn-and-skip
// under --skip-unsupported) and returns false, since the predicate cannot match.
bool ReportNoBtime(EvalContext& ctx) {
  ctx.control.unsupported = "birth time is not recorded (the filesystem or kernel does not support it)";
  return false;
}

// -newerXY (X,Y in {a,B,c,m}); -newerXt (Y=t) compares the X-time to a time string.
// Shared by every 8-char -newer* name; reads X/Y from the descriptor. When X=B and
// the entry's birth time is unrecorded the comparison is impossible (ReportNoBtime).
// A Y=B reference whose birth time is unrecorded stays a silent no-match (it is the
// reference file's filesystem, not the walked entry's, that is at issue).
bool EvalNewerXY(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const std::string_view name = expr.descriptor->name;
  const char x = name[6];
  if (x == 'B' && !ctx.visit.metadata.btime.has_value()) {
    return ReportNoBtime(ctx);  // the walked entry's birth time is required but absent
  }
  if (name[7] == 't') {
    const std::optional<absl::Time> ref = datetime::ParseTimeString(expr.args.front(), ctx.now, ctx.tz);
    const std::optional<absl::Time> field = TimeField(ctx.visit.metadata, x);
    return ref.has_value() && field.has_value() && *field > *ref;
  }
  return IsNewerXY(ctx.visit, x, name[7], expr.args.front(), ctx.fs);
}

// find's -anewer/-cnewer: the entry's access/change time is newer than the
// reference file's modification time -- the classic spellings of -neweram /
// -newercm. (-newer itself is mtime-vs-mtime, handled by EvalNewer above.)
bool EvalAnewer(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && IsNewerXY(ctx.visit, 'a', 'm', expr.args.front(), ctx.fs);
}

bool EvalCnewer(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && IsNewerXY(ctx.visit, 'c', 'm', expr.args.front(), ctx.fs);
}

bool EvalMtime(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const std::string_view arg = expr.args.front();
  if (arg.find(' ') != std::string_view::npos) {  // xff word/compound duration (e.g. "-3 weeks 3 hours")
    return MatchesAge(arg, ctx.visit.metadata.mtime, ctx.now, ctx.tz);
  }
  return MatchesTime(arg, ctx.visit.metadata.mtime, ctx.now, absl::Hours(24), /*allow_unit_suffix=*/true);
}

bool EvalMmin(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty()
         && MatchesTime(
             expr.args.front(), ctx.visit.metadata.mtime, ctx.now, absl::Minutes(1), /*allow_unit_suffix=*/false);
}

bool EvalAtime(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const std::string_view arg = expr.args.front();
  if (arg.find(' ') != std::string_view::npos) {  // xff word/compound duration
    return MatchesAge(arg, ctx.visit.metadata.atime, ctx.now, ctx.tz);
  }
  return MatchesTime(arg, ctx.visit.metadata.atime, ctx.now, absl::Hours(24), /*allow_unit_suffix=*/true);
}

bool EvalAmin(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty()
         && MatchesTime(
             expr.args.front(), ctx.visit.metadata.atime, ctx.now, absl::Minutes(1), /*allow_unit_suffix=*/false);
}

bool EvalCtime(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const std::string_view arg = expr.args.front();
  if (arg.find(' ') != std::string_view::npos) {  // xff word/compound duration
    return MatchesAge(arg, ctx.visit.metadata.ctime, ctx.now, ctx.tz);
  }
  return MatchesTime(arg, ctx.visit.metadata.ctime, ctx.now, absl::Hours(24), /*allow_unit_suffix=*/true);
}

bool EvalCmin(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty()
         && MatchesTime(
             expr.args.front(), ctx.visit.metadata.ctime, ctx.now, absl::Minutes(1), /*allow_unit_suffix=*/false);
}

// BSD's -Btime/-Bmin: the birth (creation) time, the -mtime/-mmin of `btime`. Birth
// time is optional -- only some kernels/filesystems record it -- so an entry without
// it cannot satisfy the predicate and is flagged as an impossible task (see
// ReportNoBtime); the driver fails or, under --skip-unsupported, warns and skips.
bool EvalBtime(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  if (!ctx.visit.metadata.btime.has_value()) {
    return ReportNoBtime(ctx);
  }
  const std::string_view arg = expr.args.front();
  if (arg.find(' ') != std::string_view::npos) {  // xff word/compound duration
    return MatchesAge(arg, *ctx.visit.metadata.btime, ctx.now, ctx.tz);
  }
  return MatchesTime(arg, *ctx.visit.metadata.btime, ctx.now, absl::Hours(24), /*allow_unit_suffix=*/true);
}

bool EvalBmin(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  if (!ctx.visit.metadata.btime.has_value()) {
    return ReportNoBtime(ctx);
  }
  return MatchesTime(
      expr.args.front(), *ctx.visit.metadata.btime, ctx.now, absl::Minutes(1), /*allow_unit_suffix=*/false);
}

// Record builders shared by the stdout actions (-print/-print0/-ls) and their
// file-writing counterparts (-fprint/-fprint0/-fls), so both emit identical bytes.
std::string PrintRecord(const Visit& visit) {
  return absl::StrCat(visit.path, "\n");
}

std::string Print0Record(const Visit& visit) {
  std::string record(visit.path);
  record.push_back('\0');
  return record;
}

// find's -ls: an `ls -dils`-style line -- inode, 1 KiB blocks, symbolic
// permissions, link count, owner, group, size, time, and path -- rendered in
// `tz`. Columns are single-space-separated (xff does not pad to find's widths).
std::string LsRecord(const Visit& visit, absl::Time now, absl::TimeZone tz) {
  const vfs::Metadata& md = visit.metadata;
  const std::uint64_t blocks_1k = (md.blocks + 1) / 2;  // 512-byte blocks -> 1 KiB blocks (rounded up)
  return absl::StrCat(
      md.ino, " ", blocks_1k, " ", SymbolicPerms(md.type, md.mode), " ", md.nlink, " ", UserName(md.uid), " ",
      GroupName(md.gid), " ", md.size, " ", LsTime(md.mtime, now, tz), " ", visit.path, "\n");
}

bool EvalPrint(const parser::Expr&, EvalContext& ctx) {
  ctx.emit(PrintRecord(ctx.visit));
  return true;
}

bool EvalPrint0(const parser::Expr&, EvalContext& ctx) {
  ctx.emit(Print0Record(ctx.visit));
  return true;
}

bool EvalLs(const parser::Expr&, EvalContext& ctx) {
  ctx.emit(LsRecord(ctx.visit, ctx.now, ctx.tz));
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

// find's -fprint FILE / -fprint0 FILE / -fls FILE / -fprintf FILE FORMAT: the
// -print/-print0/-ls/-printf output, written to a named file instead of stdout.
// The driver opens each file once (truncating) and appends across firings; with
// no file sink wired the actions are inert (in-process callers that pass none).
bool EvalFprint(const parser::Expr& expr, EvalContext& ctx) {
  if (!expr.args.empty() && ctx.emit_file) {
    ctx.emit_file(expr.args.front(), PrintRecord(ctx.visit));
  }
  return true;
}

bool EvalFprint0(const parser::Expr& expr, EvalContext& ctx) {
  if (!expr.args.empty() && ctx.emit_file) {
    ctx.emit_file(expr.args.front(), Print0Record(ctx.visit));
  }
  return true;
}

bool EvalFls(const parser::Expr& expr, EvalContext& ctx) {
  if (!expr.args.empty() && ctx.emit_file) {
    ctx.emit_file(expr.args.front(), LsRecord(ctx.visit, ctx.now, ctx.tz));
  }
  return true;
}

// -fprintf takes FILE then FORMAT; the format owns its own terminator, like -printf.
bool EvalFprintf(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.size() >= 2 && ctx.emit_file) {
    ctx.emit_file(expr.args.front(), FormatPrintf(expr.args[1], ctx.visit, ctx.tz));
  }
  return true;
}

bool EvalDelete(const parser::Expr&, EvalContext& ctx) {
  static_cast<void>(ctx.fs.Remove(ctx.visit.path));  // failures set a nonzero exit; wired in the exit-code work
  return true;
}

// Renders every -exec/-execdir token through the field vocabulary ({}, {name},
// {path}, {root}, {capture.*}, ...) for --exec-fields, yielding the final argv.
std::vector<std::string> RenderExecArgv(const parser::Expr& expr, const EvalContext& ctx) {
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
  return argv;
}

bool EvalExec(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.exec_batch) {
    // `-exec ... +`: queue the full path in the single global ("") bucket; the
    // command runs at end-of-walk (RunFind flushes each batch node in ARG_MAX
    // chunks). The action is true per entry.
    if (ctx.exec_batches != nullptr) {
      (*ctx.exec_batches)[&expr][""].emplace_back(ctx.visit.path);
    }
    return true;
  }
  if (ctx.parallel_exec != nullptr) {
    // -j>1: launch the child on the bounded runner. find-exact substitutes {} ->
    // path inside Launch; --exec-fields renders first (no {} remains, empty target).
    // True per entry -- the exit status is collected at the end-of-walk Drain.
    if (ctx.exec_fields) {
      ctx.parallel_exec->Launch(RenderExecArgv(expr, ctx), /*target=*/{}, /*dir=*/{});
    } else {
      ctx.parallel_exec->Launch(expr.args, ctx.visit.path, /*dir=*/{});
    }
    return true;
  }
  if (!ctx.exec_fields) {
    return exec::Execute(expr.args, ctx.visit.path);  // find-exact: only {} is substituted (-> path)
  }
  // --exec-fields: render each token through the field vocabulary, then spawn the
  // already-substituted argv.
  return exec::ExecuteArgs(RenderExecArgv(expr, ctx));  // true iff the child exits 0
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
  if (expr.exec_batch) {
    // `-execdir ... +`: queue the "./<basename>" under its directory; RunFind runs
    // the command once per directory (cwd = that dir) at end-of-walk.
    if (ctx.exec_batches != nullptr) {
      (*ctx.exec_batches)[&expr][target.dir].push_back(target.brace);
    }
    return true;
  }
  if (ctx.parallel_exec != nullptr) {
    // -j>1: launch in the entry's directory (cwd = target.dir). find-exact maps {}
    // -> ./basename inside Launch; --exec-fields renders first (no {} remains).
    if (ctx.exec_fields) {
      ctx.parallel_exec->Launch(RenderExecArgv(expr, ctx), /*target=*/{}, target.dir);
    } else {
      ctx.parallel_exec->Launch(expr.args, target.brace, target.dir);
    }
    return true;
  }
  if (!ctx.exec_fields) {
    return exec::ExecuteInDir(expr.args, target.dir, target.brace);  // find-exact: {} -> ./basename
  }
  // --exec-fields: render each token through the field vocabulary (which still
  // sees the full path/root), then spawn in the entry's directory.
  return exec::ExecuteArgsInDir(RenderExecArgv(expr, ctx), target.dir);
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
    DispatchPair{"-Bmin", {&EvalBmin}},    // capital 'B' sorts before the lowercase entries
    DispatchPair{"-Btime", {&EvalBtime}},  // (ASCII), so the birth-time pair leads the table
    DispatchPair{"-amin", {&EvalAmin}},
    DispatchPair{"-anewer", {&EvalAnewer}},
    DispatchPair{"-atime", {&EvalAtime}},
    DispatchPair{"-capture", {&EvalCapture}},
    DispatchPair{"-capturedir", {&EvalCapturedir}},
    DispatchPair{"-cmin", {&EvalCmin}},
    DispatchPair{"-cnewer", {&EvalCnewer}},
    DispatchPair{"-ctime", {&EvalCtime}},
    DispatchPair{"-delete", {&EvalDelete}},
    DispatchPair{"-empty", {&EvalEmpty}},
    DispatchPair{"-exec", {&EvalExec}},
    DispatchPair{"-execdir", {&EvalExecdir}},
    DispatchPair{"-executable", {&EvalExecutable}},
    DispatchPair{"-false", {&EvalFalse}},
    DispatchPair{"-fls", {&EvalFls}},
    DispatchPair{"-fprint", {&EvalFprint}},
    DispatchPair{"-fprint0", {&EvalFprint0}},
    DispatchPair{"-fprintf", {&EvalFprintf}},
    DispatchPair{"-fstype", {&EvalFstype}},
    DispatchPair{"-gid", {&EvalGid}},
    DispatchPair{"-group", {&EvalGroup}},
    DispatchPair{"-ilname", {&EvalLname}},
    DispatchPair{"-iname", {&EvalName}},
    DispatchPair{"-inum", {&EvalInum}},
    DispatchPair{"-ipath", {&EvalPath}},
    DispatchPair{"-iregex", {&EvalRegex}},
    DispatchPair{"-iwholename", {&EvalPath}},
    DispatchPair{"-links", {&EvalLinks}},
    DispatchPair{"-lname", {&EvalLname}},
    DispatchPair{"-ls", {&EvalLs}},
    DispatchPair{"-mmin", {&EvalMmin}},
    DispatchPair{"-mtime", {&EvalMtime}},
    DispatchPair{"-name", {&EvalName}},
    DispatchPair{"-newer", {&EvalNewer}},
    DispatchPair{"-newerBB", {&EvalNewerXY}},  // birthtime -newerXY combos (BSD-compat)
    DispatchPair{"-newerBa", {&EvalNewerXY}},
    DispatchPair{"-newerBc", {&EvalNewerXY}},
    DispatchPair{"-newerBm", {&EvalNewerXY}},
    DispatchPair{"-newerBt", {&EvalNewerXY}},
    DispatchPair{"-neweraB", {&EvalNewerXY}},
    DispatchPair{"-neweraa", {&EvalNewerXY}},
    DispatchPair{"-newerac", {&EvalNewerXY}},
    DispatchPair{"-neweram", {&EvalNewerXY}},
    DispatchPair{"-newerat", {&EvalNewerXY}},
    DispatchPair{"-newerca", {&EvalNewerXY}},
    DispatchPair{"-newercc", {&EvalNewerXY}},
    DispatchPair{"-newercm", {&EvalNewerXY}},
    DispatchPair{"-newerct", {&EvalNewerXY}},
    DispatchPair{"-newercB", {&EvalNewerXY}},
    DispatchPair{"-newermB", {&EvalNewerXY}},
    DispatchPair{"-newerma", {&EvalNewerXY}},
    DispatchPair{"-newermc", {&EvalNewerXY}},
    DispatchPair{"-newermm", {&EvalNewerXY}},
    DispatchPair{"-newermt", {&EvalNewerXY}},
    DispatchPair{"-nogroup", {&EvalNogroup}},
    DispatchPair{"-nouser", {&EvalNouser}},
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
    DispatchPair{"-readable", {&EvalReadable}},
    DispatchPair{"-regex", {&EvalRegex}},
    DispatchPair{"-samefile", {&EvalSamefile}},
    DispatchPair{"-size", {&EvalSize}},
    DispatchPair{"-sparse", {&EvalSparse}},
    DispatchPair{"-true", {&EvalTrue}},
    DispatchPair{"-type", {&EvalType}},
    DispatchPair{"-uid", {&EvalUid}},
    DispatchPair{"-used", {&EvalUsed}},
    DispatchPair{"-user", {&EvalUser}},
    DispatchPair{"-wholename", {&EvalPath}},
    DispatchPair{"-writable", {&EvalWritable}},
    DispatchPair{"-xtype", {&EvalXtype}});

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

absl::Status ValidateSizeArgs(const parser::Expr& expr) {
  if (expr.kind == parser::Expr::Kind::kPredicate) {
    if (expr.descriptor != nullptr && expr.descriptor->name == "-size" && !expr.args.empty()) {
      if (const absl::Status status = ParseSizeSpec(expr.args.front()).status(); !status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }
  if (expr.lhs != nullptr) {
    if (const absl::Status status = ValidateSizeArgs(*expr.lhs); !status.ok()) {
      return status;
    }
  }
  if (expr.rhs != nullptr) {
    if (const absl::Status status = ValidateSizeArgs(*expr.rhs); !status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

}  // namespace xff::engine
