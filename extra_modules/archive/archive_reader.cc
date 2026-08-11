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

#include "xff/archive/archive_reader.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "xff/license/notice.h"

namespace xff::archive {
namespace {

// The streaming block size for both the header walk and the content read: one value, so the two
// entry points cannot drift apart.
constexpr std::size_t kBlockSize = 64 * 1'024;

// libarchive's BSD-2-Clause notice, plus the permissive codec closure it links (zlib, bzip2,
// liblzma, lz4, zstd). Registered from this TU, so the notice appears exactly in the builds that
// link the archive extra. mbedtls is deliberately not enabled, so no crypto arm is listed.
const license::Registrar kLibarchiveNotice{{
    .component = "libarchive",
    .spdx = "BSD-2-Clause",
    .text = "Copyright (c) 2003-2024 Tim Kientzle and contributors. Includes the permissive codec "
            "libraries libarchive links: zlib (Zlib), bzip2 (bzip2-1.0.6), liblzma/xz (0BSD), "
            "lz4 (BSD-2-Clause) and zstd (BSD-3-Clause arm).",
}};

// `archive_read_free` for a unique_ptr, so every early return releases the reader.
struct ArchiveDeleter {
  void operator()(struct ::archive* handle) const noexcept {
    if (handle != nullptr) {
      ::archive_read_free(handle);
    }
  }
};

using ArchivePtr = std::unique_ptr<struct ::archive, ArchiveDeleter>;

// A reader with every format and filter libarchive was built with enabled; the format is detected
// from the content, so callers never name it.
ArchivePtr NewReader() {
  ArchivePtr handle{::archive_read_new()};
  if (handle != nullptr) {
    ::archive_read_support_filter_all(handle.get());
    ::archive_read_support_format_all(handle.get());
  }
  return handle;
}

// The reader's current error text, or a stable fallback when libarchive left none.
std::string LastError(struct ::archive* handle) {
  const char* const message = ::archive_error_string(handle);
  return message == nullptr ? std::string("unknown libarchive error") : std::string(message);
}

// Walks an opened reader's headers into Members. Shared by the memory and file entry points: only
// the open call differs. Never reads member content (headers only), so this stays cheap.
absl::StatusOr<std::vector<Member>> ReadMembers(struct ::archive* handle) {
  std::vector<Member> members;
  while (true) {
    struct ::archive_entry* entry = nullptr;
    const int status = ::archive_read_next_header(handle, &entry);
    if (status == ARCHIVE_EOF) {
      return members;
    }
    if (status < ARCHIVE_WARN) {
      // Opened as an archive but failed part way: a truncated or corrupt archive, which is a real
      // error the walk must report - distinct from "this file is not an archive at all".
      return absl::DataLossError(absl::StrCat("archive read failed: ", LastError(handle)));
    }
    const char* const path = ::archive_entry_pathname(entry);
    const mode_t filetype = ::archive_entry_filetype(entry);
    Member member{
        .path = path == nullptr ? std::string() : std::string(path),
        .size = ::archive_entry_size(entry),
        .mtime = ::archive_entry_mtime(entry),
        .mode = static_cast<std::uint32_t>(::archive_entry_perm(entry)),
        .is_directory = filetype == AE_IFDIR,
        .is_symlink = filetype == AE_IFLNK,
    };
    if (member.is_symlink) {
      const char* const target = ::archive_entry_symlink(entry);
      member.link_target = target == nullptr ? std::string() : std::string(target);
    }
    members.push_back(std::move(member));
  }
}

}  // namespace

absl::StatusOr<std::vector<Member>> ListMembers(std::string_view bytes) {
  // libarchive opens zero bytes happily and reports EOF at once, which would make an empty file
  // look like a valid archive holding nothing. No format has an empty representation, so an empty
  // input is simply not an archive - and the walk must keep treating such a file as a plain file.
  if (bytes.empty()) {
    return absl::InvalidArgumentError("not a readable archive: empty input");
  }
  const ArchivePtr handle = NewReader();
  if (handle == nullptr) {
    return absl::ResourceExhaustedError("cannot allocate a libarchive reader");
  }
  if (::archive_read_open_memory(handle.get(), bytes.data(), bytes.size()) != ARCHIVE_OK) {
    return absl::InvalidArgumentError(absl::StrCat("not a readable archive: ", LastError(handle.get())));
  }
  return ReadMembers(handle.get());
}

absl::StatusOr<std::vector<Member>> ListMembersOfFile(std::string_view path) {
  const ArchivePtr handle = NewReader();
  if (handle == nullptr) {
    return absl::ResourceExhaustedError("cannot allocate a libarchive reader");
  }
  // 64 KiB blocks: large enough to keep syscalls down on big archives, small enough that listing a
  // tiny one costs nothing. The C API needs a NUL-terminated path.
  const std::string path_string(path);
  if (::archive_read_open_filename(handle.get(), path_string.c_str(), kBlockSize) != ARCHIVE_OK) {
    return absl::InvalidArgumentError(absl::StrCat("not a readable archive: ", LastError(handle.get())));
  }
  return ReadMembers(handle.get());
}

namespace {

// The member name in comparable form. Tar writes the SAME member several ways: `dir/x` and `./dir/x`
// for a file, and a directory as `dir/` with a trailing slash - so a lookup for `dir` must find it
// (and then be told it has no content, rather than "no such member").
std::string_view NormalizedMemberName(std::string_view path) {
  while (path.starts_with("./")) {
    path.remove_prefix(2);
  }
  while (path.size() > 1 && path.ends_with('/')) {
    path.remove_suffix(1);
  }
  return path;
}

}  // namespace

absl::StatusOr<std::string> ReadMemberOfFile(std::string_view path, std::string_view member, std::uint64_t max_bytes) {
  const ArchivePtr handle = NewReader();
  if (handle == nullptr) {
    return absl::ResourceExhaustedError("cannot allocate a libarchive reader");
  }
  const std::string path_string(path);
  if (::archive_read_open_filename(handle.get(), path_string.c_str(), kBlockSize) != ARCHIVE_OK) {
    return absl::InvalidArgumentError(absl::StrCat("not a readable archive: ", LastError(handle.get())));
  }
  const std::string_view wanted = NormalizedMemberName(member);
  struct ::archive_entry* entry = nullptr;
  while (true) {
    const int status = ::archive_read_next_header(handle.get(), &entry);
    if (status == ARCHIVE_EOF) {
      return absl::NotFoundError(absl::StrCat("no such member in ", path, ": ", member));
    }
    if (status != ARCHIVE_OK && status != ARCHIVE_WARN) {
      return absl::DataLossError(absl::StrCat("archive read failed: ", LastError(handle.get())));
    }
    const char* const stored = ::archive_entry_pathname(entry);
    if (stored == nullptr || NormalizedMemberName(stored) != wanted) {
      continue;  // not this one; libarchive skips its data on the next header read
    }
    if (::archive_entry_filetype(entry) != AE_IFREG) {
      // A directory or symlink has no content. Saying so beats returning an empty string, which a
      // content predicate could not distinguish from a genuinely empty file.
      return absl::FailedPreconditionError(absl::StrCat("member is not a regular file: ", member));
    }
    std::string contents;
    // The header's size is a HINT for reserve() only - never a trusted length. A crafted archive can
    // understate it, so the loop below is what actually bounds the read.
    const std::int64_t hint = ::archive_entry_size(entry);
    if (hint > 0) {
      const std::uint64_t reserve = static_cast<std::uint64_t>(hint);
      contents.reserve(max_bytes != 0 ? std::min<std::uint64_t>(reserve, max_bytes) : reserve);
    }
    std::array<char, kBlockSize> buffer{};
    while (true) {
      const ::ssize_t read = ::archive_read_data(handle.get(), buffer.data(), buffer.size());
      if (read == 0) {
        return contents;  // end of this member's data
      }
      if (read < 0) {
        return absl::DataLossError(absl::StrCat("reading member ", member, " failed: ", LastError(handle.get())));
      }
      if (max_bytes != 0 && contents.size() + static_cast<std::uint64_t>(read) > max_bytes) {
        return absl::ResourceExhaustedError(absl::StrCat("member ", member, " exceeds the ", max_bytes, " byte limit"));
      }
      contents.append(buffer.data(), static_cast<std::size_t>(read));
    }
  }
}

}  // namespace xff::archive
