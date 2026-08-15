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

#include "xff/archive/archive_pack.h"

#include <archive.h>
#include <archive_entry.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace xff::archive {
namespace {

namespace stdfs = std::filesystem;

constexpr std::size_t kBlockSize = 64 * 1'024;

// std::filesystem::file_time_type has an unspecified epoch, and C++20's file_clock::to_sys is not
// available on every platform this builds on, so the offset is measured once against a known point.
// Both clocks tick in seconds here, which is all an archive header records anyway.
std::int64_t FileClockToUnixEpoch() {
  const auto file_now = stdfs::file_time_type::clock::now().time_since_epoch();
  const auto system_now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::seconds>(system_now).count()
         - std::chrono::duration_cast<std::chrono::seconds>(file_now).count();
}

// Closes before freeing: zip writes its central directory at close time, so a handle that is only
// freed produces a truncated archive rather than an error.
struct WriteDeleter {
  void operator()(struct ::archive* handle) const noexcept {
    if (handle != nullptr) {
      ::archive_write_close(handle);
      ::archive_write_free(handle);
    }
  }
};

struct EntryDeleter {
  void operator()(struct ::archive_entry* entry) const noexcept {
    if (entry != nullptr) {
      ::archive_entry_free(entry);
    }
  }
};

using WritePtr = std::unique_ptr<struct ::archive, WriteDeleter>;
using EntryPtr = std::unique_ptr<struct ::archive_entry, EntryDeleter>;

// One output suffix and what it selects. Longest suffix first, so `.tar.gz` is not read as `.gz`.
struct FormatSuffix {
  std::string_view suffix;
  std::string_view name;  // what PackFormats / the help calls it
  int format;             // ARCHIVE_FORMAT_*
  int filter;             // ARCHIVE_FILTER_*
  // The compressor's level range, both 0 when the format has no compressor. Checked here rather than
  // left to libarchive, which answers an out-of-range level with "Undefined option" and sends the
  // reader looking for a misspelled flag.
  int level_min;
  int level_max;
};

constexpr std::array kFormats = std::to_array<FormatSuffix>({
    {".tar.gz", "tar.gz", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_GZIP, 0, 9},
    {".tar.bz2", "tar.bz2", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_BZIP2, 1, 9},
    {".tar.xz", "tar.xz", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_XZ, 0, 9},
    {".tar.zst", "tar.zst", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_ZSTD, 1, 22},
    {".tgz", "tgz", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_GZIP, 0, 9},
    {".tar", "tar", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_NONE, 0, 0},
    {".zip", "zip", ARCHIVE_FORMAT_ZIP, ARCHIVE_FILTER_NONE, 0, 9},
});

const FormatSuffix* FormatEntryFor(std::string_view path) {
  const std::string lower = absl::AsciiStrToLower(path);
  for (const FormatSuffix& candidate : kFormats) {
    if (std::string_view(lower).ends_with(candidate.suffix)) {
      return &candidate;
    }
  }
  return nullptr;
}

// Writes one entry's header and, for a regular file, its bytes. The size has to be known up front
// (tar puts it in the header), so it is taken from the stat rather than from the read.
absl::Status WriteOne(struct ::archive* writer, const PackEntry& entry) {
  std::error_code error;
  const stdfs::path source(entry.source);
  static const std::int64_t kFileClockToUnixEpoch = FileClockToUnixEpoch();
  const stdfs::file_status status = stdfs::symlink_status(source, error);
  if (error) {
    return absl::NotFoundError(absl::StrCat("cannot stat '", entry.source, "': ", error.message()));
  }
  const EntryPtr header{::archive_entry_new()};
  if (header == nullptr) {
    return absl::UnavailableError("out of memory building an archive entry");
  }
  ::archive_entry_set_pathname(header.get(), entry.name.c_str());
  ::archive_entry_set_perm(header.get(), static_cast<::mode_t>(status.permissions()) & 0777);
  // The modification time travels with the file; without it every member unpacks stamped 1970, which
  // breaks the make / rsync comparisons an archive of source files exists for. Ownership deliberately
  // does NOT travel: it is stored as 0:0, the reproducible-archive convention, so unpacking as root
  // cannot hand files to whoever happened to own them on the machine that packed them.
  if (const stdfs::file_time_type when = stdfs::last_write_time(source, error); !error) {
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(when.time_since_epoch()).count() + kFileClockToUnixEpoch;
    ::archive_entry_set_mtime(header.get(), static_cast<::time_t>(seconds), 0);
  }
  error.clear();
  if (stdfs::is_symlink(status)) {
    const stdfs::path target = stdfs::read_symlink(source, error);
    if (error) {
      return absl::NotFoundError(absl::StrCat("cannot read the link '", entry.source, "': ", error.message()));
    }
    ::archive_entry_set_filetype(header.get(), AE_IFLNK);
    ::archive_entry_set_symlink(header.get(), target.string().c_str());
  } else if (stdfs::is_directory(status)) {
    ::archive_entry_set_filetype(header.get(), AE_IFDIR);
  } else {
    const std::uintmax_t size = stdfs::file_size(source, error);
    if (error) {
      return absl::NotFoundError(absl::StrCat("cannot size '", entry.source, "': ", error.message()));
    }
    ::archive_entry_set_filetype(header.get(), AE_IFREG);
    ::archive_entry_set_size(header.get(), static_cast<::la_int64_t>(size));
  }
  if (::archive_write_header(writer, header.get()) < ARCHIVE_WARN) {
    return absl::UnavailableError(
        absl::StrCat("cannot write the entry '", entry.name, "': ", ::archive_error_string(writer)));
  }
  if (!stdfs::is_regular_file(status)) {
    return absl::OkStatus();  // a directory or link has a header and no content
  }
  // Streamed in blocks: an archive may hold files far larger than memory.
  std::FILE* const file = std::fopen(entry.source.c_str(), "rb");
  if (file == nullptr) {
    return absl::NotFoundError(absl::StrCat("cannot open '", entry.source, "'"));
  }
  std::array<char, kBlockSize> buffer{};
  for (;;) {
    const std::size_t read = std::fread(buffer.data(), 1, buffer.size(), file);
    if (read == 0) {
      break;
    }
    if (::archive_write_data(writer, buffer.data(), read) < 0) {
      const std::string message(::archive_error_string(writer));
      (void)std::fclose(file);
      return absl::UnavailableError(absl::StrCat("cannot write '", entry.name, "': ", message));
    }
  }
  const bool failed = std::ferror(file) != 0;
  (void)std::fclose(file);
  if (failed) {
    return absl::DataLossError(absl::StrCat("read failed part way through '", entry.source, "'"));
  }
  return absl::OkStatus();
}

}  // namespace

std::string FormatFromName(std::string_view path) {
  const FormatSuffix* const found = FormatEntryFor(path);
  return found == nullptr ? std::string() : std::string(found->name);
}

std::vector<std::string> PackFormats() {
  std::vector<std::string> names;
  names.reserve(kFormats.size());
  for (const FormatSuffix& format : kFormats) {
    names.emplace_back(format.name);
  }
  return names;
}

absl::Status PackFiles(std::string_view path, const std::vector<PackEntry>& entries, const PackSettings& options) {
  const FormatSuffix* const format = FormatEntryFor(path);
  if (format == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat(
            "cannot tell the archive format from '", path, "'; expected one ending in ",
            absl::StrJoin(PackFormats(), ", ")));
  }
  // Written beside the target and renamed over it, so a failure part way leaves no half archive -
  // and an existing file survives an attempt that fails. Same contract as the member rewrite.
  const stdfs::path target(path);
  const stdfs::path temporary = stdfs::path(target).concat(".xff-pack");
  {
    const WritePtr writer{::archive_write_new()};
    if (writer == nullptr) {
      return absl::UnavailableError("out of memory creating the archive writer");
    }
    if (::archive_write_set_format(writer.get(), format->format) != ARCHIVE_OK
        || (format->filter != ARCHIVE_FILTER_NONE
            && ::archive_write_add_filter(writer.get(), format->filter) != ARCHIVE_OK)) {
      return absl::InvalidArgumentError(
          absl::StrCat("this build cannot write ", format->name, ": ", ::archive_error_string(writer.get())));
    }
    if (options.level.has_value()) {
      if (format->level_max == 0) {
        return absl::InvalidArgumentError(
            absl::StrCat(format->name, " is not compressed, so there is no compression level to set"));
      }
      if (*options.level < format->level_min || *options.level > format->level_max) {
        return absl::InvalidArgumentError(
            absl::StrCat(
                "compression level ", *options.level, " is out of range for ", format->name, ", which accepts ",
                format->level_min, "-", format->level_max));
      }
      const std::string setting = absl::StrCat("compression-level=", *options.level);
      if (::archive_write_set_options(writer.get(), setting.c_str()) != ARCHIVE_OK) {
        return absl::InvalidArgumentError(
            absl::StrCat(
                "cannot set the ", format->name, " compression level to ", *options.level, ": ",
                ::archive_error_string(writer.get())));
      }
    }
    if (::archive_write_open_filename(writer.get(), temporary.string().c_str()) != ARCHIVE_OK) {
      return absl::UnavailableError(
          absl::StrCat("cannot create '", temporary.string(), "': ", ::archive_error_string(writer.get())));
    }
    for (const PackEntry& entry : entries) {
      if (const absl::Status status = WriteOne(writer.get(), entry); !status.ok()) {
        std::error_code ignored;
        stdfs::remove(temporary, ignored);
        return status;
      }
    }
  }  // the deleter closes the writer, which is when zip writes its directory
  std::error_code error;
  stdfs::rename(temporary, target, error);
  if (error) {
    std::error_code ignored;
    stdfs::remove(temporary, ignored);
    return absl::UnavailableError(absl::StrCat("cannot place '", path, "': ", error.message()));
  }
  return absl::OkStatus();
}

}  // namespace xff::archive
