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

// POSIX (opendir/lstat/st_atim/d_type/...) and statx() are hidden by glibc
// under the strict `-std=c++23` we build with; request them explicitly. No
// effect on macOS, whose headers expose POSIX regardless of language mode.
#if defined(__linux__) && !defined(_GNU_SOURCE)
# define _GNU_SOURCE 1
#endif

#include "xff/vfs/local_fs.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::vfs {
namespace {

FileType TypeFromMode(mode_t mode) {
  if (S_ISREG(mode)) {
    return FileType::kRegular;
  }
  if (S_ISDIR(mode)) {
    return FileType::kDirectory;
  }
  if (S_ISLNK(mode)) {
    return FileType::kSymlink;
  }
  if (S_ISBLK(mode)) {
    return FileType::kBlockDevice;
  }
  if (S_ISCHR(mode)) {
    return FileType::kCharDevice;
  }
  if (S_ISFIFO(mode)) {
    return FileType::kFifo;
  }
  if (S_ISSOCK(mode)) {
    return FileType::kSocket;
  }
  return FileType::kUnknown;
}

FileType TypeFromDirent(unsigned char d_type) {
  switch (d_type) {
    case DT_REG: return FileType::kRegular;
    case DT_DIR: return FileType::kDirectory;
    case DT_LNK: return FileType::kSymlink;
    case DT_BLK: return FileType::kBlockDevice;
    case DT_CHR: return FileType::kCharDevice;
    case DT_FIFO: return FileType::kFifo;
    case DT_SOCK: return FileType::kSocket;
    default: return FileType::kUnknown;
  }
}

std::string JoinPath(std::string_view dir, std::string_view name) {
  if (dir.empty()) {
    return std::string(name);
  }
  if (dir.back() == '/') {
    return absl::StrCat(dir, name);
  }
  return absl::StrCat(dir, "/", name);
}

// Birth/creation time, where the platform + filesystem record it.
std::optional<absl::Time> BirthTime(
    [[maybe_unused]] const struct stat& st,
    [[maybe_unused]] const std::string& path,
    [[maybe_unused]] bool follow_symlinks) {
#if defined(__APPLE__)
  return absl::TimeFromTimespec(st.st_birthtimespec);
#elif defined(__linux__) && defined(STATX_BTIME)
  struct statx stx;
  const int flags = follow_symlinks ? 0 : AT_SYMLINK_NOFOLLOW;
  if (::statx(AT_FDCWD, path.c_str(), flags, STATX_BTIME, &stx) == 0 && (stx.stx_mask & STATX_BTIME) != 0U) {
    struct timespec ts;
    ts.tv_sec = static_cast<time_t>(stx.stx_btime.tv_sec);
    ts.tv_nsec = static_cast<long>(stx.stx_btime.tv_nsec);  // NOLINT(google-runtime-int)
    return absl::TimeFromTimespec(ts);
  }
  return std::nullopt;
#else
  return std::nullopt;
#endif
}

Metadata MetadataFromStat(const struct stat& st, std::optional<absl::Time> btime) {
  Metadata md;
  md.type = TypeFromMode(st.st_mode);
  md.size = static_cast<std::uint64_t>(st.st_size);
  md.blocks = static_cast<std::uint64_t>(st.st_blocks);  // 512-byte blocks, for -ls
  md.mode = static_cast<std::uint32_t>(st.st_mode);
  md.nlink = static_cast<std::uint64_t>(st.st_nlink);
  md.uid = static_cast<std::uint32_t>(st.st_uid);
  md.gid = static_cast<std::uint32_t>(st.st_gid);
  md.ino = static_cast<std::uint64_t>(st.st_ino);
  md.dev = static_cast<std::uint64_t>(st.st_dev);
#if defined(__APPLE__)
  md.atime = absl::TimeFromTimespec(st.st_atimespec);
  md.mtime = absl::TimeFromTimespec(st.st_mtimespec);
  md.ctime = absl::TimeFromTimespec(st.st_ctimespec);
#else
  md.atime = absl::TimeFromTimespec(st.st_atim);
  md.mtime = absl::TimeFromTimespec(st.st_mtim);
  md.ctime = absl::TimeFromTimespec(st.st_ctim);
#endif
  md.btime = btime;
  return md;
}

}  // namespace

absl::StatusOr<std::vector<Entry>> LocalFs::ReadDir(std::string_view dir) const {
  const std::string dir_str(dir);
  DIR* dirp = ::opendir(dir_str.c_str());
  if (dirp == nullptr) {
    return absl::ErrnoToStatus(errno, absl::StrCat("opendir('", dir, "')"));
  }

  std::vector<Entry> entries;
  errno = 0;
  for (const struct dirent* de = ::readdir(dirp); de != nullptr; de = ::readdir(dirp)) {
    const std::string_view name(de->d_name);
    if (name == "." || name == "..") {
      errno = 0;
      continue;
    }
    Entry entry;
    entry.path = JoinPath(dir, name);
    entry.name = std::string(name);
    entry.type = TypeFromDirent(de->d_type);
    entry.source = Source::kLocalFs;
    entry.read_only = false;
    entries.push_back(std::move(entry));
    errno = 0;
  }
  const int read_errno = errno;
  ::closedir(dirp);
  if (read_errno != 0) {
    return absl::ErrnoToStatus(read_errno, absl::StrCat("readdir('", dir, "')"));
  }
  return entries;
}

absl::StatusOr<Metadata> LocalFs::Stat(std::string_view path, bool follow_symlinks) const {
  const std::string path_str(path);
  struct stat st{};
  const int rc = follow_symlinks ? ::stat(path_str.c_str(), &st) : ::lstat(path_str.c_str(), &st);
  if (rc != 0) {
    return absl::ErrnoToStatus(errno, absl::StrCat(follow_symlinks ? "stat('" : "lstat('", path, "')"));
  }
  return MetadataFromStat(st, BirthTime(st, path_str, follow_symlinks));
}

absl::Status LocalFs::Remove(std::string_view path) const {
  const std::string path_str(path);
  if (::remove(path_str.c_str()) != 0) {  // unlink for files/symlinks, rmdir for empty dirs
    return absl::ErrnoToStatus(errno, absl::StrCat("remove('", path, "')"));
  }
  return absl::OkStatus();
}

}  // namespace xff::vfs
