// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
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

#include "xff/fuse/fuse_metadata.h"

#include <unistd.h>

#include <cerrno>

#include "absl/time/time.h"

namespace xff::fuse {

int ErrnoForStatus(absl::StatusCode code) {
  switch (code) {
    case absl::StatusCode::kFailedPrecondition:
    case absl::StatusCode::kInvalidArgument: return EINVAL;
    case absl::StatusCode::kNotFound: return ENOENT;
    case absl::StatusCode::kPermissionDenied: return EACCES;
    case absl::StatusCode::kUnimplemented: return ENOTSUP;
    default: return EIO;
  }
}

mode_t ModeBitsForFileType(vfs::FileType type) {
  switch (type) {
    case vfs::FileType::kBlockDevice: return S_IFBLK;
    case vfs::FileType::kCharDevice: return S_IFCHR;
    case vfs::FileType::kDirectory: return S_IFDIR;
    case vfs::FileType::kFifo: return S_IFIFO;
    case vfs::FileType::kRegular: return S_IFREG;
    case vfs::FileType::kSocket: return S_IFSOCK;
    case vfs::FileType::kSymlink: return S_IFLNK;
    case vfs::FileType::kUnknown: return S_IFREG;
  }
  return S_IFREG;
}

struct stat StatForMetadata(const vfs::Metadata& metadata, std::uint64_t ino) {
  struct stat out = {};
  out.st_ino = ino;
  unsigned int mode = metadata.mode & 07777U;
  // Containers often store directories without search bits (0644, or 0); under the mount's
  // default_permissions the kernel would then refuse path traversal, so derive x from r the way
  // archive extractors do. Symlink modes are ignored on Linux and conventionally 0777.
  if (metadata.type == vfs::FileType::kDirectory) {
    mode |= (mode & 0444U) >> 2U;
  }
  if (metadata.type == vfs::FileType::kSymlink) {
    mode |= 0777U;
  }
  unsigned int stat_mode = static_cast<unsigned int>(ModeBitsForFileType(metadata.type)) | mode;
  // The mount is read-only by construction; regular permission bits say so too, whatever the
  // container stored. Symlink permissions do not grant access to their target and conventionally
  // remain 0777.
  if (metadata.type != vfs::FileType::kSymlink) {
    stat_mode &= ~0222U;
  }
  out.st_mode = static_cast<mode_t>(stat_mode);
  out.st_nlink = metadata.nlink == 0 ? 1 : static_cast<nlink_t>(metadata.nlink);
  out.st_uid = ::getuid();
  out.st_gid = ::getgid();
  out.st_size = static_cast<off_t>(metadata.size);
  out.st_blocks = static_cast<blkcnt_t>(metadata.blocks);
  const timespec mtime = absl::ToTimespec(metadata.mtime);
  const timespec atime = absl::ToTimespec(metadata.atime);
  const timespec ctime = absl::ToTimespec(metadata.ctime);
#if defined(__APPLE__)
  out.st_mtimespec = mtime;
  out.st_atimespec = atime;
  out.st_ctimespec = ctime;
#else
  out.st_mtim = mtime;
  out.st_atim = atime;
  out.st_ctim = ctime;
#endif
  return out;
}

}  // namespace xff::fuse
