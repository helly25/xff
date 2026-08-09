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

#ifndef XFF_ARCHIVE_ARCHIVE_READER_H_
#define XFF_ARCHIVE_ARCHIVE_READER_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"

namespace xff::archive {

// One member of an archive, as the reader sees it. Deliberately a plain description, not a VFS
// entry: the VFS backend (a later slice) maps these onto vfs::Metadata, so this layer stays a thin,
// testable shell over libarchive with no knowledge of the core's types.
struct Member {
  std::string path;           // member path as stored (`dir/file.txt`), never the container's path
  std::int64_t size = 0;      // UNCOMPRESSED (logical) size in bytes
  std::int64_t mtime = 0;     // modification time, seconds since the Unix epoch (0 when unset)
  std::uint32_t mode = 0;     // POSIX permission bits as stored
  bool is_directory = false;  // a directory member (an explicit entry, not merely a path prefix)
  bool is_symlink = false;    // a symlink member; `link_target` is what it points at
  std::string link_target;
};

// Lists the members of the archive in `bytes` (a whole in-memory archive). libarchive detects the
// format and the compression filter by content, so tar / zip / cpio / ar and the gz / bz2 / xz /
// zstd / lz4 filters all work through this one entry point.
//
// Returns InvalidArgumentError when the data is not an archive libarchive can open, or DataLossError
// when it opens but fails part way (a truncated or corrupt archive), so a caller can tell "not an
// archive" from "a broken archive" - the walk treats the first as a plain file and reports the
// second. Never reads member CONTENT, only the headers, so listing a huge archive stays cheap.
absl::StatusOr<std::vector<Member>> ListMembers(std::string_view bytes);

// Lists the members of the archive file at `path`, streaming it from disk rather than requiring the
// whole archive in memory (the form the walk uses). Same error contract as ListMembers.
absl::StatusOr<std::vector<Member>> ListMembersOfFile(std::string_view path);

}  // namespace xff::archive

#endif  // XFF_ARCHIVE_ARCHIVE_READER_H_
