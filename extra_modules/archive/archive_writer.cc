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

#include "xff/archive/archive_writer.h"

#include <archive.h>
#include <archive_entry.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "xff/archive/member_path.h"

namespace xff::archive {
namespace {

namespace stdfs = std::filesystem;

constexpr std::size_t kBlockSize = 64 * 1'024;

// `archive_read_free` / `archive_write_free` for a unique_ptr, so every early return releases the
// handle - and, for the writer, closes it first, since a format like zip writes its directory at
// close time.
struct ReadDeleter {
  void operator()(struct ::archive* handle) const noexcept {
    if (handle != nullptr) {
      ::archive_read_free(handle);
    }
  }
};

struct WriteDeleter {
  void operator()(struct ::archive* handle) const noexcept {
    if (handle != nullptr) {
      ::archive_write_free(handle);
    }
  }
};

using ReadPtr = std::unique_ptr<struct ::archive, ReadDeleter>;
using WritePtr = std::unique_ptr<struct ::archive, WriteDeleter>;

std::string LastError(struct ::archive* handle) {
  const char* message = ::archive_error_string(handle);
  return message == nullptr ? std::string("unknown libarchive error") : std::string(message);
}

// The same format set the reader registers, and for the same reason: `mtree` and `raw` bid on
// anything, so a text file must not present as an archive here either.
ReadPtr OpenReader(const std::string& path) {
  ReadPtr handle{::archive_read_new()};
  if (handle == nullptr) {
    return handle;
  }
  ::archive_read_support_filter_all(handle.get());
  ::archive_read_support_format_7zip(handle.get());
  ::archive_read_support_format_ar(handle.get());
  ::archive_read_support_format_cab(handle.get());
  ::archive_read_support_format_cpio(handle.get());
  ::archive_read_support_format_empty(handle.get());
  ::archive_read_support_format_iso9660(handle.get());
  ::archive_read_support_format_lha(handle.get());
  ::archive_read_support_format_rar(handle.get());
  ::archive_read_support_format_rar5(handle.get());
  ::archive_read_support_format_tar(handle.get());
  ::archive_read_support_format_warc(handle.get());
  ::archive_read_support_format_xar(handle.get());
  ::archive_read_support_format_zip(handle.get());
  if (::archive_read_open_filename(handle.get(), path.c_str(), kBlockSize) != ARCHIVE_OK) {
    return nullptr;
  }
  return handle;
}

// Copies one member's data across. Block-wise rather than whole-member, so rewriting a container
// holding a huge member does not need it in memory. `archive_write_data` and not the _block variant:
// the latter belongs to the write-to-DISK API and answers "not supported" on an archive writer.
absl::Status CopyData(struct ::archive* reader, struct ::archive* writer) {
  for (;;) {
    const void* block = nullptr;
    std::size_t size = 0;
    std::int64_t offset = 0;
    const int status = ::archive_read_data_block(reader, &block, &size, &offset);
    if (status == ARCHIVE_EOF) {
      return absl::OkStatus();
    }
    if (status < ARCHIVE_WARN) {
      return absl::DataLossError(LastError(reader));
    }
    if (::archive_write_data(writer, block, size) < 0) {
      return absl::UnavailableError(LastError(writer));
    }
  }
}

// The rewrite itself, into an already-created writer. Split out so the caller owns the temporary
// file's lifetime: whatever happens here, an unfinished temporary is removed rather than renamed.
absl::Status RewriteWithout(
    struct ::archive* reader,
    struct ::archive* writer,
    const std::vector<std::string>& members,
    std::vector<bool>* removed) {
  for (;;) {
    struct ::archive_entry* entry = nullptr;
    const int status = ::archive_read_next_header(reader, &entry);
    if (status == ARCHIVE_EOF) {
      return absl::OkStatus();
    }
    if (status < ARCHIVE_WARN) {
      return absl::DataLossError(LastError(reader));
    }
    const char* stored = ::archive_entry_pathname(entry);
    const std::string_view name = NormalizeMemberName(stored == nullptr ? std::string_view() : stored);
    bool drop = false;
    for (std::size_t i = 0; i < members.size(); ++i) {
      if (NormalizeMemberName(members[i]) == name) {
        drop = true;
        (*removed)[i] = true;
      }
    }
    if (drop) {
      ::archive_read_data_skip(reader);
      continue;
    }
    if (::archive_write_header(writer, entry) < ARCHIVE_WARN) {
      return absl::UnavailableError(LastError(writer));
    }
    if (::archive_entry_size(entry) > 0) {
      const absl::Status copied = CopyData(reader, writer);
      if (!copied.ok()) {
        return copied;
      }
    }
  }
}

}  // namespace

absl::Status RemoveMembersOfFile(std::string_view path, const std::vector<std::string>& members) {
  if (members.empty()) {
    return absl::OkStatus();
  }
  const std::string path_string(path);
  const ReadPtr reader = OpenReader(path_string);
  if (reader == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat("not an archive this build can open: ", path));
  }
  // The format and filter are read off the ORIGINAL, which needs a header, so peek at the first
  // member before the writer is set up. An archive with no members has nothing to remove anyway.
  struct ::archive_entry* first = nullptr;
  const int peeked = ::archive_read_next_header(reader.get(), &first);
  if (peeked == ARCHIVE_EOF) {
    return absl::NotFoundError(absl::StrCat("no such member in ", path, ": ", absl::StrJoin(members, ", ")));
  }
  if (peeked < ARCHIVE_WARN) {
    return absl::DataLossError(LastError(reader.get()));
  }
  const stdfs::path target(path_string);
  const stdfs::path temporary = stdfs::path(target).replace_filename(target.filename().string() + ".xff-rewrite");
  {
    const WritePtr writer{::archive_write_new()};
    if (writer == nullptr) {
      return absl::UnavailableError("cannot create a libarchive writer");
    }
    // Reading a format does not imply writing it: libarchive reads 7-Zip, RAR, ISO and cab, and
    // writes only some of those. Refusing here (rather than producing a tar named `.7z`) is what
    // keeps a rewrite from quietly changing the container's format.
    if (::archive_write_set_format(writer.get(), ::archive_format(reader.get())) != ARCHIVE_OK) {
      return absl::UnimplementedError(
          absl::StrCat(
              "this build cannot write the ", ::archive_format_name(reader.get()),
              " format back, so a member cannot be removed from ", path));
    }
    // Filter 0 is the outermost compression the reader applied, and the innermost is always `none`.
    // Carrying it over is what keeps a `.tar.gz` gzipped rather than silently expanding it.
    for (int i = ::archive_filter_count(reader.get()) - 2; i >= 0; --i) {
      if (::archive_write_add_filter(writer.get(), ::archive_filter_code(reader.get(), i)) != ARCHIVE_OK) {
        return absl::UnimplementedError(
            absl::StrCat(
                "this build cannot write the ", ::archive_filter_name(reader.get(), i),
                " compression back, so a member cannot be removed from ", path));
      }
    }
    if (::archive_write_open_filename(writer.get(), temporary.string().c_str()) != ARCHIVE_OK) {
      return absl::UnavailableError(absl::StrCat("cannot write ", temporary.string(), ": ", LastError(writer.get())));
    }
    std::vector<bool> removed(members.size(), false);
    // The peeked header is the first member, so handle it before the loop takes over the rest.
    absl::Status status = absl::OkStatus();
    const char* stored = ::archive_entry_pathname(first);
    const std::string_view name = NormalizeMemberName(stored == nullptr ? std::string_view() : stored);
    bool drop = false;
    for (std::size_t i = 0; i < members.size(); ++i) {
      if (NormalizeMemberName(members[i]) == name) {
        drop = true;
        removed[i] = true;
      }
    }
    if (drop) {
      ::archive_read_data_skip(reader.get());
    } else if (::archive_write_header(writer.get(), first) < ARCHIVE_WARN) {
      status = absl::UnavailableError(LastError(writer.get()));
    } else if (::archive_entry_size(first) > 0) {
      status = CopyData(reader.get(), writer.get());
    }
    if (status.ok()) {
      status = RewriteWithout(reader.get(), writer.get(), members, &removed);
    }
    if (status.ok() && !absl::c_all_of(removed, [](bool found) { return found; })) {
      std::vector<std::string> missing;
      for (std::size_t i = 0; i < members.size(); ++i) {
        if (!removed[i]) {
          missing.push_back(members[i]);
        }
      }
      status = absl::NotFoundError(absl::StrCat("no such member in ", path, ": ", absl::StrJoin(missing, ", ")));
    }
    if (status.ok() && ::archive_write_close(writer.get()) != ARCHIVE_OK) {
      // Close is where a zip writes its central directory, so a failure here is a failure to write.
      status = absl::UnavailableError(LastError(writer.get()));
    }
    if (!status.ok()) {
      std::error_code ignored;
      stdfs::remove(temporary, ignored);  // nothing half-written survives
      return status;
    }
  }
  std::error_code error;
  const stdfs::perms mode = stdfs::status(target, error).permissions();
  stdfs::rename(temporary, target, error);
  if (error) {
    std::error_code ignored;
    stdfs::remove(temporary, ignored);
    return absl::UnavailableError(absl::StrCat("cannot replace ", path, ": ", error.message()));
  }
  stdfs::permissions(target, mode, error);  // the replacement is the same file to its user
  return absl::OkStatus();
}

}  // namespace xff::archive
