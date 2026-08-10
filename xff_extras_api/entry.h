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

#ifndef XFF_VFS_ENTRY_H_
#define XFF_VFS_ENTRY_H_

#include <cstdint>
#include <optional>
#include <string>

#include "absl/time/time.h"

namespace xff::vfs {

// The kind of a filesystem entry. Mirrors POSIX `d_type` / `st_mode` file types
// so the engine can answer find's `-type` without re-deriving from raw bits.
enum class FileType {
  kUnknown,  // type not yet resolved (e.g. `readdir` returned `DT_UNKNOWN`)
  kRegular,
  kDirectory,
  kSymlink,
  kBlockDevice,
  kCharDevice,
  kFifo,
  kSocket,
};

// Where an entry comes from. Only `kLocalFs` exists today; archive members and
// remote files are read-only virtual entries that slot in behind the same
// `FileSystem` interface later (design.md "Virtual entries: archives & remote").
enum class Source {
  kLocalFs,
  kArchiveMember,
  kRemote,
};

// File metadata sufficient for find's predicates, normalized off POSIX `stat`.
// Permission/owner fields use fixed-width types so the interface stays
// platform-agnostic (the engine never includes <sys/stat.h>). `btime` (birth /
// creation time) is optional: not every kernel/filesystem records it, so the
// predicate layer decides whether absence is a hard error (design.md
// "macOS / cross-platform correctness").
struct Metadata {
  FileType type = FileType::kUnknown;
  std::uint64_t size = 0;    // logical size in bytes
  std::uint64_t blocks = 0;  // 512-byte blocks actually allocated (st_blocks); backs -ls
  std::uint32_t mode = 0;    // permission + type bits (st_mode), masked by the engine
  std::uint64_t nlink = 0;
  std::uint32_t uid = 0;
  std::uint32_t gid = 0;
  std::uint64_t ino = 0;
  std::uint64_t dev = 0;
  absl::Time atime;                 // last access
  absl::Time mtime;                 // last modification
  absl::Time ctime;                 // last inode change (NOT creation)
  std::optional<absl::Time> btime;  // birth / creation time, where recorded
};

// A single child produced by directory iteration. `path` is what the traversal
// uses downstream (the real filesystem path for `kLocalFs`); `name` is the
// final component. `type` comes from `readdir` and may be `kUnknown` when the
// filesystem does not report `d_type` - callers `Stat` to resolve it.
struct Entry {
  std::string path;
  std::string name;
  FileType type = FileType::kUnknown;
  Source source = Source::kLocalFs;
  bool read_only = false;  // true for virtual (archive / remote) entries
};

}  // namespace xff::vfs

#endif  // XFF_VFS_ENTRY_H_
