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

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "absl/time/time.h"
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

// Resolves a field name + optional qualifier to its value; unknown -> empty.
std::string ResolveField(
    std::string_view name, std::string_view qualifier, std::string_view path, const vfs::Metadata& metadata,
    int depth) {
  const stdfs::path fs_path{std::string(path)};
  if (name == "path") return std::string(path);
  if (name == "dir") {
    const std::string parent = fs_path.parent_path().string();
    return parent.empty() ? "." : parent;  // find's %h is "." when there is no directory part
  }
  if (name == "name" || name == "file") return fs_path.filename().string();
  if (name == "stem") return fs_path.stem().string();
  if (name == "ext" || name == "extension") {
    const std::string ext = fs_path.extension().string();  // includes the leading '.'
    return ext.empty() ? ext : ext.substr(1);
  }
  if (name == "suffixes") {  // all extensions, e.g. ".tar.gz"; a leading dot is not one
    const std::string filename = fs_path.filename().string();
    const std::string::size_type dot = filename.find('.', 1);
    return dot == std::string::npos ? "" : filename.substr(dot);
  }
  if (name == "depth") return std::to_string(depth);
  if (name == "size") return qualifier == "h" ? HumanSize(metadata.size) : std::to_string(metadata.size);
  if (name == "type") return std::string(1, TypeLetter(metadata.type));
  if (name == "inode") return std::to_string(metadata.ino);
  if (name == "links") return std::to_string(metadata.nlink);
  if (name == "mtime") return FormatTimeField(metadata.mtime, qualifier);
  if (name == "atime") return FormatTimeField(metadata.atime, qualifier);
  if (name == "ctime") return FormatTimeField(metadata.ctime, qualifier);
  if (name == "btime") return metadata.btime.has_value() ? FormatTimeField(*metadata.btime, qualifier) : "";
  if (name == "mode" || name == "perm") return OctalPerm(metadata.mode);
  if (name == "user") return OwnerName(metadata.uid);
  if (name == "group") return GroupName(metadata.gid);
  return "";  // unknown field -> empty ({root} is the remaining follow-up)
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

}  // namespace

std::string Render(std::string_view tmpl, std::string_view path, const vfs::Metadata& metadata, int depth) {
  std::string out;
  for (std::string_view::size_type i = 0; i < tmpl.size();) {
    const char ch = tmpl[i];
    if (ch == '{' && i + 1 < tmpl.size() && tmpl[i + 1] == '{') {
      out.push_back('{');
      i += 2;
    } else if (ch == '}' && i + 1 < tmpl.size() && tmpl[i + 1] == '}') {
      out.push_back('}');
      i += 2;
    } else if (ch == '{') {
      std::string_view name;
      std::string qualifier;
      const std::string_view::size_type next = ParseField(tmpl, i, name, qualifier);
      if (next == std::string_view::npos) {  // not a well-formed placeholder -> literal '{'
        out.push_back(ch);
        ++i;
        continue;
      }
      out.append(ResolveField(name, qualifier, path, metadata, depth));
      i = next;
    } else {
      out.push_back(ch);
      ++i;
    }
  }
  return out;
}

}  // namespace xff::fields
