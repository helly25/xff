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

// getpwuid()/getgrgid() are POSIX, hidden by glibc under the strict -std=c++23
// build; request them explicitly. No effect on macOS.
#if defined(__linux__) && !defined(_GNU_SOURCE)
# define _GNU_SOURCE 1
#endif

#include "xff/fields/fields.h"

#include <grp.h>
#include <pwd.h>

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "mbo/container/limited_map.h"
#include "xff/datetime/datetime.h"
#include "xff/regex/regex.h"
#include "xff/vfs/entry.h"

namespace xff::fields {
namespace {

namespace stdfs = std::filesystem;

// find's -printf %y / the {type} field letter, keyed by file type (kUnknown and any
// unmapped value fall through to 'U'). A constexpr map, per the style's preference
// for a uniform key -> value mapping over a switch.
using TypeLetterPair = std::pair<vfs::FileType, char>;
constexpr auto kTypeLetters = mbo::container::MakeLimitedMap(
    TypeLetterPair{vfs::FileType::kBlockDevice, 'b'},
    TypeLetterPair{vfs::FileType::kCharDevice, 'c'},
    TypeLetterPair{vfs::FileType::kDirectory, 'd'},
    TypeLetterPair{vfs::FileType::kFifo, 'p'},
    TypeLetterPair{vfs::FileType::kRegular, 'f'},
    TypeLetterPair{vfs::FileType::kSocket, 's'},
    TypeLetterPair{vfs::FileType::kSymlink, 'l'});

char TypeLetter(vfs::FileType type) {
  const auto it = kTypeLetters.find(type);
  return it == kTypeLetters.end() ? 'U' : it->second;  // kUnknown / unmapped -> 'U'
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

// The ls -l / stat %A symbolic access string for {access}: a type char followed by
// the owner/group/other rwx triples, with setuid/setgid ('s'/'S') and sticky
// ('t'/'T') folded onto the execute positions. Type char is ls-style ('-' for a
// regular file, not the {type} letter 'f').
std::string AccessString(vfs::FileType type, std::uint32_t mode) {
  std::string out;
  switch (type) {  // alphabetical by enum name
    case vfs::FileType::kBlockDevice: out.push_back('b'); break;
    case vfs::FileType::kCharDevice: out.push_back('c'); break;
    case vfs::FileType::kDirectory: out.push_back('d'); break;
    case vfs::FileType::kFifo: out.push_back('p'); break;
    case vfs::FileType::kRegular: out.push_back('-'); break;
    case vfs::FileType::kSocket: out.push_back('s'); break;
    case vfs::FileType::kSymlink: out.push_back('l'); break;
    case vfs::FileType::kUnknown: out.push_back('?'); break;
  }
  // One rwx triple: r, w, then x with the special (setid/sticky) bit folded in.
  const auto triple = [&out, mode](unsigned r, unsigned w, unsigned x, unsigned special, char set, char clr) {
    out.push_back((mode & r) != 0 ? 'r' : '-');
    out.push_back((mode & w) != 0 ? 'w' : '-');
    const bool exec = (mode & x) != 0;
    out.push_back((mode & special) != 0 ? (exec ? set : clr) : (exec ? 'x' : '-'));
  };
  triple(0400U, 0200U, 0100U, 04000U, 's', 'S');  // owner + setuid
  triple(0040U, 0020U, 0010U, 02000U, 's', 'S');  // group + setgid
  triple(0004U, 0002U, 0001U, 01000U, 't', 'T');  // other + sticky
  return out;
}

// Owner / group name from the password / group database, falling back to the
// numeric id when there is no entry (matching find's %u/%g).
std::string OwnerName(std::uint32_t uid) {
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

// Formats a timestamp for a time field. The qualifier is a datetime preset name
// (find/iso/space/epoch) or a custom absl::FormatTime pattern; empty defaults to
// the "space" ISO form. Rendered in `tz` (the local zone unless --timezone).
std::string FormatTimeField(absl::Time time, std::string_view qualifier, absl::TimeZone tz) {
  return datetime::FormatTime(time, qualifier, tz);
}

// Human-readable size ({size:h}): bytes under 1 KiB as a plain count, otherwise
// a 1024-based unit with one (truncated) decimal, e.g. 1536 -> "1.5K".
std::string HumanSize(std::uint64_t bytes) {
  if (bytes < 1'024) {
    return std::to_string(bytes);
  }
  static constexpr std::string_view kUnits = "KMGTPE";
  std::uint64_t scale = 1'024;
  int unit = 0;
  while (bytes >= scale * 1'024 && unit + 1 < 6) {
    scale *= 1'024;
    ++unit;
  }
  std::string out = std::to_string(bytes / scale);
  out.push_back('.');
  out.push_back(static_cast<char>('0' + (bytes % scale) * 10 / scale));
  out.push_back(kUnits[unit]);
  return out;
}

// Per-field renderers. The signature is uniform so they can share one dispatch
// table: (key, qualifier, ctx). `key` is the bound argument for dynamic fields
// (a capture index, an {env.NAME} var, ...), unused (unnamed) by the builtins.
// `path`-derived fields build their own std::filesystem::path.
std::string PathField(std::string_view, std::string_view, const RenderContext& ctx) {
  return std::string(ctx.path);
}

std::string RootField(std::string_view, std::string_view, const RenderContext& ctx) {
  return std::string(ctx.root);  // command-line search root (find %H); empty when unset
}

// {target}: a symlink's target text (find %l), the driver having resolved it via
// ReadLink; empty for a non-symlink. Composes with the path-component qualifier
// ({target:name}, {target:core}, ...) like any path-valued field.
std::string TargetField(std::string_view, std::string_view, const RenderContext& ctx) {
  return std::string(ctx.link_target);
}

// {relpath}: the entry's path relative to the search root it was reached from
// (find's %P) -- the root prefix and its trailing separator removed. Empty for the
// root operand itself, and (best-effort) the whole path when no root is recorded or
// the path is not root-prefixed. Mirrors engine::RelativeTo so `-cmp`/`-diff` targets
// like '{def.B}/{relpath}' address the parallel entry under another tree.
std::string RelpathField(std::string_view, std::string_view, const RenderContext& ctx) {
  const std::string_view path = ctx.path;
  const std::string_view root = ctx.root;
  if (root.empty() || path == root) {
    return path == root ? std::string() : std::string(path);
  }
  if (path.size() > root.size() && path.substr(0, root.size()) == root) {
    std::string_view rest = path.substr(root.size());
    while (!rest.empty() && rest.front() == '/') {
      rest.remove_prefix(1);
    }
    return std::string(rest);
  }
  return std::string(path);  // not root-prefixed (should not happen from the walk)
}

std::string DirField(std::string_view, std::string_view, const RenderContext& ctx) {
  const std::string parent = stdfs::path(std::string(ctx.path)).parent_path().string();
  return parent.empty() ? "." : parent;  // find's %h is "." when there is no directory part
}

std::string NameField(std::string_view, std::string_view, const RenderContext& ctx) {
  return stdfs::path(std::string(ctx.path)).filename().string();
}

std::string StemField(std::string_view, std::string_view, const RenderContext& ctx) {
  return stdfs::path(std::string(ctx.path)).stem().string();
}

std::string ExtField(std::string_view, std::string_view, const RenderContext& ctx) {
  const std::string ext = stdfs::path(std::string(ctx.path)).extension().string();  // includes the leading '.'
  return ext.empty() ? ext : ext.substr(1);
}

std::string SuffixesField(std::string_view, std::string_view, const RenderContext& ctx) {
  const std::string filename = stdfs::path(std::string(ctx.path)).filename().string();
  const std::string::size_type dot = filename.find('.', 1);  // all extensions; a leading dot is not one
  return dot == std::string::npos ? "" : filename.substr(dot);
}

// {suffix}: the LAST extension WITH its leading dot (pathlib's .suffix: ".gz"),
// complementing {ext} (no dot: "gz") and {suffixes} (all of them: ".tar.gz").
std::string SuffixField(std::string_view, std::string_view, const RenderContext& ctx) {
  return stdfs::path(std::string(ctx.path)).extension().string();  // ".gz", or "" when none
}

// {core}: the filename with ALL extensions removed (foo.tar.gz -> foo), the complement
// of {suffixes} ({core} + {suffixes} == {name}, as {stem} + {suffix} == {name}).
std::string CoreField(std::string_view, std::string_view, const RenderContext& ctx) {
  const std::string filename = stdfs::path(std::string(ctx.path)).filename().string();
  const std::string::size_type dot = filename.find('.', 1);  // first extension; a leading dot is not one
  return dot == std::string::npos ? filename : filename.substr(0, dot);
}

std::string DepthField(std::string_view, std::string_view, const RenderContext& ctx) {
  return std::to_string(ctx.depth);
}

// {line} / {text}: the 1-based number and text of the current -grep match line;
// both empty outside a -grep line (line_number unset), so they no-op elsewhere.
std::string LineField(std::string_view, std::string_view, const RenderContext& ctx) {
  return ctx.line_number.has_value() ? std::to_string(*ctx.line_number) : std::string();
}

std::string TextField(std::string_view, std::string_view, const RenderContext& ctx) {
  return ctx.line_number.has_value() ? std::string(ctx.line_text) : std::string();
}

// {match} / {column}: the matched substring on a -grep line (grep -o) and its
// 1-based byte column; empty/absent unless the driver computed a match span.
std::string MatchField(std::string_view, std::string_view, const RenderContext& ctx) {
  return ctx.match_column.has_value() ? std::string(ctx.match_text) : std::string();
}

std::string ColumnField(std::string_view, std::string_view, const RenderContext& ctx) {
  return ctx.match_column.has_value() ? std::to_string(*ctx.match_column) : std::string();
}

std::string SizeField(std::string_view, std::string_view qualifier, const RenderContext& ctx) {
  return qualifier == "h" ? HumanSize(ctx.metadata.size) : std::to_string(ctx.metadata.size);
}

// {blocks}: allocated 512-byte blocks (st_blocks, find's %b); {blocks:h} the
// human-readable allocated bytes. The on-disk counterpart to {size} (apparent).
std::string BlocksField(std::string_view, std::string_view qualifier, const RenderContext& ctx) {
  return qualifier == "h" ? HumanSize(ctx.metadata.blocks * 512U) : std::to_string(ctx.metadata.blocks);
}

std::string TypeField(std::string_view, std::string_view, const RenderContext& ctx) {
  return std::string(1, TypeLetter(ctx.metadata.type));
}

std::string InodeField(std::string_view, std::string_view, const RenderContext& ctx) {
  return std::to_string(ctx.metadata.ino);
}

std::string LinksField(std::string_view, std::string_view, const RenderContext& ctx) {
  return std::to_string(ctx.metadata.nlink);
}

// A bare {mtime} (no {:qualifier}) uses the --time-format default (ctx.time_format,
// itself empty -> "space"); an explicit {mtime:iso} qualifier always wins.
std::string MtimeField(std::string_view, std::string_view qualifier, const RenderContext& ctx) {
  return FormatTimeField(ctx.metadata.mtime, qualifier.empty() ? ctx.time_format : qualifier, ctx.tz);
}

std::string AtimeField(std::string_view, std::string_view qualifier, const RenderContext& ctx) {
  return FormatTimeField(ctx.metadata.atime, qualifier.empty() ? ctx.time_format : qualifier, ctx.tz);
}

std::string CtimeField(std::string_view, std::string_view qualifier, const RenderContext& ctx) {
  return FormatTimeField(ctx.metadata.ctime, qualifier.empty() ? ctx.time_format : qualifier, ctx.tz);
}

std::string BtimeField(std::string_view, std::string_view qualifier, const RenderContext& ctx) {
  const std::string_view spec = qualifier.empty() ? ctx.time_format : qualifier;
  return ctx.metadata.btime.has_value() ? FormatTimeField(*ctx.metadata.btime, spec, ctx.tz) : "";
}

std::string ModeField(std::string_view, std::string_view, const RenderContext& ctx) {
  return OctalPerm(ctx.metadata.mode);
}

std::string UserField(std::string_view, std::string_view, const RenderContext& ctx) {
  return OwnerName(ctx.metadata.uid);
}

std::string GroupField(std::string_view, std::string_view, const RenderContext& ctx) {
  return GroupName(ctx.metadata.gid);
}

// {uid} / {gid}: numeric owner / group ids (find's %U/%G), complementing the name
// fields {user}/{group}. {dev}: the device number (find's %D). {access}: the ls -l /
// stat %A symbolic permission string, complementing the octal {mode}/{perm}.
std::string UidField(std::string_view, std::string_view, const RenderContext& ctx) {
  return std::to_string(ctx.metadata.uid);
}

std::string GidField(std::string_view, std::string_view, const RenderContext& ctx) {
  return std::to_string(ctx.metadata.gid);
}

std::string DevField(std::string_view, std::string_view, const RenderContext& ctx) {
  return std::to_string(ctx.metadata.dev);
}

std::string AccessField(std::string_view, std::string_view, const RenderContext& ctx) {
  return AccessString(ctx.metadata.type, ctx.metadata.mode);
}

std::string EmptyField(std::string_view, std::string_view, const RenderContext&) {
  return "";  // unknown field -> empty
}

// Constexpr field-name -> renderer table, built once at compile time via mbo's
// LimitedMap. Aliases (file/name, ext/extension, mode/perm) share a renderer;
// the empty name backs {}, find's full-path placeholder (an alias for {path}).
using FieldEntry = std::pair<std::string_view, detail::FieldFn>;
constexpr auto kFieldTable = mbo::container::MakeLimitedMap(
    FieldEntry{"", &PathField},  // {} -> full path (find's -exec placeholder)
    FieldEntry{"access", &AccessField},
    FieldEntry{"atime", &AtimeField},
    FieldEntry{"blocks", &BlocksField},
    FieldEntry{"btime", &BtimeField},
    FieldEntry{"column", &ColumnField},
    FieldEntry{"core", &CoreField},
    FieldEntry{"ctime", &CtimeField},
    FieldEntry{"depth", &DepthField},
    FieldEntry{"dev", &DevField},
    FieldEntry{"dir", &DirField},
    FieldEntry{"ext", &ExtField},
    FieldEntry{"extension", &ExtField},
    FieldEntry{"file", &NameField},
    FieldEntry{"gid", &GidField},
    FieldEntry{"group", &GroupField},
    FieldEntry{"inode", &InodeField},
    FieldEntry{"line", &LineField},
    FieldEntry{"links", &LinksField},
    FieldEntry{"match", &MatchField},
    FieldEntry{"mode", &ModeField},
    FieldEntry{"mtime", &MtimeField},
    FieldEntry{"name", &NameField},
    FieldEntry{"path", &PathField},
    FieldEntry{"perm", &ModeField},
    FieldEntry{"relpath", &RelpathField},
    FieldEntry{"root", &RootField},
    FieldEntry{"size", &SizeField},
    FieldEntry{"stem", &StemField},
    FieldEntry{"suffix", &SuffixField},
    FieldEntry{"suffixes", &SuffixesField},
    FieldEntry{"target", &TargetField},
    FieldEntry{"text", &TextField},
    FieldEntry{"type", &TypeField},
    FieldEntry{"uid", &UidField},
    FieldEntry{"user", &UserField});

// Resolves a field name to its renderer; an unknown name renders empty.
detail::FieldFn LookupField(std::string_view name) {
  const auto it = kFieldTable.find(name);
  return it == kFieldTable.end() ? &EmptyField : it->second;
}

// Scans a "{name[:qualifier]}" placeholder beginning at tmpl[start] == '{'. On
// success returns the index just past the closing '}', with `name` pointing into
// `tmpl` and `qualifier` holding the (dequoted) qualifier. A qualifier may be a
// "C-quoted string" so it can carry a literal '}' or ':' (\" and \\ are escapes).
// Returns npos when there is no well-formed placeholder (the '{' stays literal).
std::string_view::size_type ParseField(
    std::string_view tmpl,
    std::string_view::size_type start,
    std::string_view& name,
    std::string& qualifier) {
  std::string_view::size_type pos = start + 1;
  const std::string_view::size_type name_begin = pos;
  while (pos < tmpl.size() && tmpl[pos] != ':' && tmpl[pos] != '}') {
    ++pos;
  }
  if (pos >= tmpl.size()) {
    return std::string_view::npos;  // no terminator
  }
  name = tmpl.substr(name_begin, pos - name_begin);
  if (tmpl[pos] == '}') {  // no qualifier
    qualifier.clear();
    return pos + 1;
  }
  ++pos;                                        // consume ':'
  if (pos < tmpl.size() && tmpl[pos] == '"') {  // quoted qualifier
    ++pos;                                      // consume opening '"'
    std::string value;
    while (pos < tmpl.size() && tmpl[pos] != '"') {
      if (tmpl[pos] == '\\' && pos + 1 < tmpl.size() && (tmpl[pos + 1] == '"' || tmpl[pos + 1] == '\\')) {
        value.push_back(tmpl[pos + 1]);
        pos += 2;
      } else {
        value.push_back(tmpl[pos]);
        ++pos;
      }
    }
    if (pos >= tmpl.size() || pos + 1 >= tmpl.size() || tmpl[pos + 1] != '}') {
      return std::string_view::npos;  // unterminated quote, or no '}' right after -> literal
    }
    qualifier = std::move(value);
    return pos + 2;  // past closing '"' and '}'
  }
  const std::string_view::size_type end = tmpl.find('}', pos);  // unquoted qualifier
  if (end == std::string_view::npos) {
    return std::string_view::npos;
  }
  qualifier.assign(tmpl.substr(pos, end - pos));
  return end + 1;
}

// Parses a field name that is a run of digits ({0},{1},...) into a capture index;
// returns -1 when `name` is empty or has a non-digit (i.e. not a capture ref).
int CaptureIndex(std::string_view name) {
  if (name.empty()) {
    return -1;  // {} is the path alias, not a capture
  }
  int value = 0;
  for (const char ch : name) {
    if (ch < '0' || ch > '9') {
      return -1;
    }
    value = value * 10 + (ch - '0');
  }
  return value;
}

// Renders a numeric {0}..{N} placeholder: `key` is the digit run; reads the
// matching regex capture from the context ([0] whole match, 1..N groups), empty
// when captures are unset or the index is out of range.
std::string CaptureField(std::string_view key, std::string_view, const RenderContext& ctx) {
  const int index = CaptureIndex(key);
  if (ctx.captures == nullptr || index < 0 || index >= static_cast<int>(ctx.captures->size())) {
    return "";
  }
  return (*ctx.captures)[static_cast<std::size_t>(index)];
}

// Renders {env.NAME}: `key` is NAME; the process environment value, or empty
// when unset. std::getenv is standard C++ (no POSIX feature-test needed).
std::string EnvField(std::string_view key, std::string_view, const RenderContext&) {
  const std::string name(key);
  const char* const value = std::getenv(name.c_str());
  return value == nullptr ? "" : value;
}

// Renders {def.NAME}: `key` is NAME; the --define value, or empty when undefined.
std::string DefField(std::string_view key, std::string_view, const RenderContext& ctx) {
  if (ctx.defines == nullptr) {
    return "";
  }
  const auto it = ctx.defines->find(std::string(key));
  return it == ctx.defines->end() ? "" : it->second;
}

// Renders {capture.NAME}: `key` is NAME; a -capture result, empty when unset.
std::string OutputField(std::string_view key, std::string_view, const RenderContext& ctx) {
  if (ctx.outputs == nullptr) {
    return "";
  }
  const auto it = ctx.outputs->find(std::string(key));
  return it == ctx.outputs->end() ? "" : it->second;
}

// Resolves a placeholder name to a renderer and its bound key: a numeric
// {0}..{N} -> a regex capture; the {env.NAME} namespace -> the environment;
// otherwise a builtin field from the table (empty key). New namespaces
// ({def.*}, {capture.*}) slot in here.
std::pair<detail::FieldFn, std::string> ResolveName(std::string_view name) {
  if (CaptureIndex(name) >= 0) {
    return {&CaptureField, std::string(name)};
  }
  if (name.starts_with("env.")) {
    return {&EnvField, std::string(name.substr(4))};
  }
  if (name.starts_with("def.")) {
    return {&DefField, std::string(name.substr(4))};
  }
  if (name.starts_with("capture.")) {
    return {&OutputField, std::string(name.substr(8))};
  }
  return {LookupField(name), std::string()};
}

// A qualifier is a sed-style rewrite when it is `s` followed by a punctuation
// delimiter (s/.../.../, s#...#...#, ...) -- distinct from the format qualifiers
// (h, iso, epoch, a strftime %..., or a quoted "...").
bool IsRewriteQualifier(std::string_view qualifier) {
  return qualifier.size() >= 2 && qualifier[0] == 's' && std::ispunct(static_cast<unsigned char>(qualifier[1])) != 0;
}

// Applies a sed-style rewrite `s<delim>PAT<delim>REPL<delim>[flags]` to `value`:
// an RE2 substitution with `g` (all matches) and `i` (case-insensitive) flags. A
// malformed spec or uncompilable pattern leaves the value unchanged.
std::string ApplyRewrite(std::string_view value, std::string_view spec) {
  const char delim = spec[1];  // caller guarantees a rewrite spec (size >= 2)
  const std::string_view rest = spec.substr(2);
  const std::string_view::size_type pat_end = rest.find(delim);
  if (pat_end == std::string_view::npos) {
    return std::string(value);
  }
  const std::string_view pattern = rest.substr(0, pat_end);
  const std::string_view after = rest.substr(pat_end + 1);
  const std::string_view::size_type repl_end = after.find(delim);
  if (repl_end == std::string_view::npos) {
    return std::string(value);
  }
  const std::string_view replacement = after.substr(0, repl_end);
  const std::string_view flags = after.substr(repl_end + 1);
  const absl::StatusOr<regex::Matcher> matcher =
      regex::Matcher::Compile(pattern, /*case_insensitive=*/flags.find('i') != std::string_view::npos);
  if (!matcher.ok()) {
    return std::string(value);
  }
  return matcher->Rewrite(value, replacement, /*global=*/flags.find('g') != std::string_view::npos);
}

// The path-component qualifier keywords ({field:KEYWORD}). Sorted (for readability).
constexpr auto kPathComponents = std::to_array<std::string_view>(
    {"basename", "core", "dir", "ext", "extension", "file", "name", "path", "stem", "suffix", "suffixes"});

// A qualifier is a path-component extraction when it is one of the keywords above --
// applied post-render (treating the field's value as a path), like the s/// rewrite.
bool IsPathComponent(std::string_view qualifier) {
  return absl::c_contains(kPathComponents, qualifier);
}

// Treats `value` as a path and extracts `component`, mirroring the flat path fields
// so any path-valued field composes: {target:stem}, {path:name}, {def.B:dir}, ...
// {core} = the filename with ALL extensions removed (foo.tar.gz -> foo), the
// complement of {suffixes}; {path} (and any unknown keyword) is the identity.
std::string PathComponent(std::string_view value, std::string_view component) {
  const stdfs::path path{std::string(value)};
  if (component == "dir") {
    const std::string parent = path.parent_path().string();
    return parent.empty() ? "." : parent;
  }
  if (component == "name" || component == "basename" || component == "file") {
    return path.filename().string();
  }
  if (component == "stem") {
    return path.stem().string();
  }
  if (component == "ext" || component == "extension") {
    const std::string ext = path.extension().string();  // includes the leading '.'
    return ext.empty() ? ext : ext.substr(1);
  }
  if (component == "suffix") {
    return path.extension().string();  // last extension WITH its dot
  }
  const std::string filename = path.filename().string();
  const std::string::size_type dot = filename.find('.', 1);  // first extension; a leading dot is not one
  if (component == "suffixes") {
    return dot == std::string::npos ? "" : filename.substr(dot);
  }
  if (component == "core") {
    return dot == std::string::npos ? filename : filename.substr(0, dot);
  }
  return std::string(value);  // "path" (whole) or an unrecognised keyword -> identity
}

}  // namespace

Template Template::Compile(std::string_view tmpl) {
  Template compiled;
  std::string literal;
  const auto flush_literal = [&] {
    if (!literal.empty()) {
      compiled.segments_.push_back({.literal = std::move(literal)});
      literal.clear();  // restore the moved-from buffer to a known-empty state
    }
  };
  for (std::string_view::size_type i = 0; i < tmpl.size();) {
    const char ch = tmpl[i];
    if (ch == '{' && i + 1 < tmpl.size() && tmpl[i + 1] == '{') {
      literal.push_back('{');
      i += 2;
    } else if (ch == '}' && i + 1 < tmpl.size() && tmpl[i + 1] == '}') {
      literal.push_back('}');
      i += 2;
    } else if (ch == '{') {
      std::string_view name;
      std::string qualifier;
      const std::string_view::size_type next = ParseField(tmpl, i, name, qualifier);
      if (next == std::string_view::npos) {  // not a well-formed placeholder -> literal '{'
        literal.push_back(ch);
        ++i;
        continue;
      }
      flush_literal();
      auto [fn, key] = ResolveName(name);  // builtin field, {0}..{N} capture, or {env.NAME}
      // Classify the qualifier: an s/// rewrite or a path-component extraction is a
      // post-render transform; anything else is the field's own format argument.
      const Segment::PostProcess post = IsRewriteQualifier(qualifier) ? Segment::PostProcess::kRewrite
                                        : IsPathComponent(qualifier)  ? Segment::PostProcess::kComponent
                                                                      : Segment::PostProcess::kNone;
      compiled.segments_.push_back({.fn = fn, .key = std::move(key), .qualifier = std::move(qualifier), .post = post});
      i = next;
    } else {
      literal.push_back(ch);
      ++i;
    }
  }
  flush_literal();
  return compiled;
}

std::string Template::Render(const RenderContext& context) const {
  std::string out;
  for (const Segment& segment : segments_) {
    if (segment.fn != nullptr) {
      // A post-render transform (s/// rewrite or path-component) renders the field
      // with no qualifier, then transforms the value; otherwise the qualifier is the
      // field's own format argument.
      const bool transform = segment.post != Segment::PostProcess::kNone;
      const std::string value = segment.fn(segment.key, transform ? std::string_view{} : segment.qualifier, context);
      switch (segment.post) {
        case Segment::PostProcess::kComponent: out.append(PathComponent(value, segment.qualifier)); break;
        case Segment::PostProcess::kNone: out.append(value); break;
        case Segment::PostProcess::kRewrite: out.append(ApplyRewrite(value, segment.qualifier)); break;
      }
    } else {
      out.append(segment.literal);
    }
  }
  return out;
}

std::string Render(std::string_view tmpl, std::string_view path, const vfs::Metadata& metadata, int depth) {
  return Template::Compile(tmpl).Render(RenderContext{.path = path, .metadata = metadata, .depth = depth});
}

std::string Render(std::string_view tmpl, const RenderContext& context) {
  return Template::Compile(tmpl).Render(context);
}

std::vector<FieldDoc> FieldDocs() {
  return {
      // Path & name.
      {.name = "path",
       .aliases = {},
       .group = "path",
       .header = "Path & name",
       .summary = "full path as traversed ({} is an alias)"},
      {.name = "relpath",
       .aliases = {},
       .group = "path",
       .header = "Path & name",
       .summary = "path relative to the search root (find %P)"},
      {.name = "root",
       .aliases = {},
       .group = "path",
       .header = "Path & name",
       .summary = "the search root it was reached from (find %H)"},
      {.name = "dir",
       .aliases = {},
       .group = "path",
       .header = "Path & name",
       .summary = "directory containing the entry"},
      {.name = "name",
       .aliases = {"file"},
       .group = "path",
       .header = "Path & name",
       .summary = "final path component (the file name)"},
      {.name = "stem",
       .aliases = {},
       .group = "path",
       .header = "Path & name",
       .summary = "name without its last extension"},
      {.name = "core",
       .aliases = {},
       .group = "path",
       .header = "Path & name",
       .summary = "name without all extensions (foo.tar.gz -> foo)"},
      {.name = "ext",
       .aliases = {"extension"},
       .group = "path",
       .header = "Path & name",
       .summary = "last extension, no dot (gz)"},
      {.name = "suffix",
       .aliases = {},
       .group = "path",
       .header = "Path & name",
       .summary = "last extension, with dot (.gz)"},
      {.name = "suffixes",
       .aliases = {},
       .group = "path",
       .header = "Path & name",
       .summary = "all extensions, with dots (.tar.gz)"},
      {.name = "target",
       .aliases = {},
       .group = "path",
       .header = "Path & name",
       .summary = "a symlink's target (find %l); else empty"},
      // Type & size.
      {.name = "type",
       .aliases = {},
       .group = "type",
       .header = "Type & size",
       .summary = "entry type letter (f, d, l, ...)"},
      {.name = "size",
       .aliases = {},
       .group = "type",
       .header = "Type & size",
       .summary = "size in bytes ({size:h} human-readable)"},
      {.name = "blocks",
       .aliases = {},
       .group = "type",
       .header = "Type & size",
       .summary = "512-byte blocks allocated"},
      {.name = "inode", .aliases = {}, .group = "type", .header = "Type & size", .summary = "inode number"},
      {.name = "links", .aliases = {}, .group = "type", .header = "Type & size", .summary = "hard-link count"},
      {.name = "dev", .aliases = {}, .group = "type", .header = "Type & size", .summary = "device number"},
      {.name = "depth",
       .aliases = {},
       .group = "type",
       .header = "Type & size",
       .summary = "depth below the root (0 at a root operand)"},
      // Owner & mode.
      {.name = "user", .aliases = {}, .group = "owner", .header = "Owner & mode", .summary = "owner user name"},
      {.name = "group", .aliases = {}, .group = "owner", .header = "Owner & mode", .summary = "owner group name"},
      {.name = "uid", .aliases = {}, .group = "owner", .header = "Owner & mode", .summary = "owner numeric user id"},
      {.name = "gid", .aliases = {}, .group = "owner", .header = "Owner & mode", .summary = "owner numeric group id"},
      {.name = "mode",
       .aliases = {"perm"},
       .group = "owner",
       .header = "Owner & mode",
       .summary = "permission bits, octal"},
      {.name = "access",
       .aliases = {},
       .group = "owner",
       .header = "Owner & mode",
       .summary = "symbolic permissions (ls -l / stat %A)"},
      // Time (each takes an optional {:FORMAT} qualifier; see below).
      {.name = "atime", .aliases = {}, .group = "time", .header = "Time", .summary = "last access time"},
      {.name = "mtime", .aliases = {}, .group = "time", .header = "Time", .summary = "last modification time"},
      {.name = "ctime", .aliases = {}, .group = "time", .header = "Time", .summary = "inode change time"},
      {.name = "btime",
       .aliases = {},
       .group = "time",
       .header = "Time",
       .summary = "creation/birth time (where supported)"},
      // Grep context (populated only during -grep=FORMAT; empty elsewhere).
      {.name = "line",
       .aliases = {},
       .group = "grep",
       .header = "Grep context",
       .summary = "1-based number of the matching line"},
      {.name = "text", .aliases = {}, .group = "grep", .header = "Grep context", .summary = "the full matching line"},
      {.name = "match",
       .aliases = {},
       .group = "grep",
       .header = "Grep context",
       .summary = "the matched substring (grep -o)"},
      {.name = "column",
       .aliases = {},
       .group = "grep",
       .header = "Grep context",
       .summary = "1-based column of the match"},
  };
}

std::vector<std::string_view> FieldNames() {
  std::vector<std::string_view> names;
  names.reserve(kFieldTable.size());
  for (const auto& [name, fn] : kFieldTable) {
    names.push_back(name);
  }
  return names;
}

bool IsKnownField(std::string_view spec) {
  const std::string_view name = spec.substr(0, spec.find(':'));  // strip an optional :qualifier
  return LookupField(name) != &EmptyField                        // a builtin field (incl. "" -> {} path alias)
         || CaptureIndex(name) >= 0                              // a {0}..{N} regex capture
         || name.starts_with("env.") || name.starts_with("def.") || name.starts_with("capture.");
}

std::vector<std::string_view> PathComponentKeywords() {
  return {kPathComponents.begin(), kPathComponents.end()};
}

}  // namespace xff::fields
