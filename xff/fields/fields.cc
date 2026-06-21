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
#define _GNU_SOURCE 1
#endif

#include "xff/fields/fields.h"

#include <grp.h>
#include <pwd.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/time/time.h"
#include "mbo/container/limited_map.h"
#include "xff/vfs/entry.h"

namespace xff::fields {
namespace {

namespace stdfs = std::filesystem;

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

// Formats a timestamp for a time field: "epoch" -> Unix seconds; "iso" or an
// empty qualifier -> ISO-8601 local time; otherwise a strftime format string.
// find/xff render times in local time (like find's %t).
std::string FormatTimeField(absl::Time time, std::string_view qualifier) {
  if (qualifier == "epoch") {
    return std::to_string(absl::ToUnixSeconds(time));
  }
  const std::string format =
      (qualifier.empty() || qualifier == "iso") ? "%Y-%m-%dT%H:%M:%S%z" : std::string(qualifier);
  return absl::FormatTime(format, time, absl::LocalTimeZone());
}

// Human-readable size ({size:h}): bytes under 1 KiB as a plain count, otherwise
// a 1024-based unit with one (truncated) decimal, e.g. 1536 -> "1.5K".
std::string HumanSize(std::uint64_t bytes) {
  if (bytes < 1024) {
    return std::to_string(bytes);
  }
  static constexpr char kUnits[] = "KMGTPE";
  std::uint64_t scale = 1024;
  int unit = 0;
  while (bytes >= scale * 1024 && unit + 1 < 6) {
    scale *= 1024;
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
std::string DepthField(std::string_view, std::string_view, const RenderContext& ctx) {
  return std::to_string(ctx.depth);
}
std::string SizeField(std::string_view, std::string_view qualifier, const RenderContext& ctx) {
  return qualifier == "h" ? HumanSize(ctx.metadata.size) : std::to_string(ctx.metadata.size);
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
std::string MtimeField(std::string_view, std::string_view qualifier, const RenderContext& ctx) {
  return FormatTimeField(ctx.metadata.mtime, qualifier);
}
std::string AtimeField(std::string_view, std::string_view qualifier, const RenderContext& ctx) {
  return FormatTimeField(ctx.metadata.atime, qualifier);
}
std::string CtimeField(std::string_view, std::string_view qualifier, const RenderContext& ctx) {
  return FormatTimeField(ctx.metadata.ctime, qualifier);
}
std::string BtimeField(std::string_view, std::string_view qualifier, const RenderContext& ctx) {
  return ctx.metadata.btime.has_value() ? FormatTimeField(*ctx.metadata.btime, qualifier) : "";
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
std::string EmptyField(std::string_view, std::string_view, const RenderContext&) {
  return "";  // unknown field -> empty
}

// Constexpr field-name -> renderer table, built once at compile time via mbo's
// LimitedMap. Aliases (file/name, ext/extension, mode/perm) share a renderer;
// the empty name backs {}, find's full-path placeholder (an alias for {path}).
using FieldEntry = std::pair<std::string_view, detail::FieldFn>;
constexpr auto kFieldTable = mbo::container::MakeLimitedMap(
    FieldEntry{"", &PathField},  // {} -> full path (find's -exec placeholder)
    FieldEntry{"atime", &AtimeField}, FieldEntry{"btime", &BtimeField}, FieldEntry{"ctime", &CtimeField},
    FieldEntry{"depth", &DepthField}, FieldEntry{"dir", &DirField}, FieldEntry{"ext", &ExtField},
    FieldEntry{"extension", &ExtField}, FieldEntry{"file", &NameField}, FieldEntry{"group", &GroupField},
    FieldEntry{"inode", &InodeField}, FieldEntry{"links", &LinksField}, FieldEntry{"mode", &ModeField},
    FieldEntry{"mtime", &MtimeField}, FieldEntry{"name", &NameField}, FieldEntry{"path", &PathField},
    FieldEntry{"perm", &ModeField}, FieldEntry{"root", &RootField}, FieldEntry{"size", &SizeField},
    FieldEntry{"stem", &StemField}, FieldEntry{"suffixes", &SuffixesField}, FieldEntry{"type", &TypeField},
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
    std::string_view tmpl, std::string_view::size_type start, std::string_view& name, std::string& qualifier) {
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
  ++pos;  // consume ':'
  if (pos < tmpl.size() && tmpl[pos] == '"') {  // quoted qualifier
    ++pos;  // consume opening '"'
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
      if (CaptureIndex(name) >= 0) {  // {0}..{N} -> regex capture, rendered by CaptureField from `key`
        compiled.segments_.push_back(
            {.fn = &CaptureField, .key = std::string(name), .qualifier = std::move(qualifier)});
      } else {
        compiled.segments_.push_back({.fn = LookupField(name), .qualifier = std::move(qualifier)});
      }
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
      out.append(segment.fn(segment.key, segment.qualifier, context));
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

}  // namespace xff::fields
