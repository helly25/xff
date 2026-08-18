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

#ifndef XFF_FUSE_FUSE_ABI_H_
#define XFF_FUSE_FUSE_ABI_H_

// The fuse3 lowlevel ABI, declared HERE rather than included from libfuse.
//
// WHY: libfuse's `include/` is LGPL-2.1 (its other files are GPL-2.0), and xff ships statically
// linked single-file binaries. Compiling against those headers put us in the position of ARGUING
// that LGPL-2.1 section 5 permits it (it does: data structure layouts and accessors are outside the
// restrictions, and the ten-line limit in that section applies only to inline functions). Declaring
// the interface ourselves removes the argument entirely: no libfuse file is read, compiled, linked
// or shipped, so there is no analysis left to get wrong. The library is still never linked - the
// loader dlopens whatever the host provides (libfuse3 on Linux and BSD, macFUSE on macOS), each
// under its own terms.
//
// WHAT IT MEANS FOR US: an ABI description is only as good as its fidelity, so two rules hold here.
//
//   1. Anything we never dereference is an INCOMPLETE type. `fuse_session`, `fuse_req` and
//      `fuse_conn_info` are only ever passed through, so their layouts are irrelevant AND absent.
//      What is not written cannot drift.
//   2. Anything whose fields we touch carries the exact layout, field for field, in libfuse's own
//      order, transcribed against fuse-3.18.2. Order IS the ABI.
//
// HOW A MISTAKE SURFACES: the loader resolves every entry point by NAME (see fuse_loader.h), so a
// wrong signature or field offset cannot be caught at link time - it corrupts at runtime. The guard
// is Linux CI, where fuse_server_test and the --archive-mount CLI cases drive a real fuse3: a
// mismatched `fuse_lowlevel_ops` prefix or a shifted `fuse_file_info::fh` fails there loudly.
//
// EXTENDING IT: fields are only ever APPENDED to `fuse_lowlevel_ops`, and fuse_server passes
// `op_size` as the offset just past the LAST op it implements (never `sizeof`), so this prefix means
// the same thing to every 3.x runtime. Implementing a further op means appending the ops between it
// and the current end, in libfuse's order, and moving that offset.

#include <cstdint>
#include <sys/stat.h>
#include <sys/types.h>

extern "C" {

// Passed through only, so deliberately incomplete: no layout to get wrong.
struct fuse_session;
struct fuse_req;
struct fuse_conn_info;

using fuse_req_t = struct fuse_req*;
using fuse_ino_t = std::uint64_t;

// The root inode number every fuse filesystem must answer for.
enum : fuse_ino_t { FUSE_ROOT_ID = 1 };

// Built by us and handed to fuse_session_new, which copies it; freeing is the caller's job.
struct fuse_args {
  int argc;
  char** argv;
  int allocated;
};

// Filled by our lookup: the inode, its attributes, and how long the kernel may cache both.
struct fuse_entry_param {
  fuse_ino_t ino;
  std::uint64_t generation;
  struct stat attr;
  double attr_timeout;
  double entry_timeout;
};

// Per-open file state. We read `flags` and set `fh` and `keep_cache`; every other field is here
// ONLY to place those three correctly. The nine one-bit flags plus `padding : 23` fill the first
// word, and `padding2` / `padding3` are full words despite the bitfield spelling - reproduced as
// libfuse writes them so the offsets match on every platform its own header does.
struct fuse_file_info {
  std::int32_t flags;
  std::uint32_t writepage : 1;
  std::uint32_t direct_io : 1;
  std::uint32_t keep_cache : 1;
  std::uint32_t flush : 1;
  std::uint32_t nonseekable : 1;
  std::uint32_t flock_release : 1;
  std::uint32_t cache_readdir : 1;
  std::uint32_t noflush : 1;
  std::uint32_t parallel_direct_writes : 1;
  std::uint32_t padding : 23;
  std::uint32_t padding2 : 32;
  std::uint32_t padding3 : 32;
  std::uint64_t fh;
  std::uint64_t lock_owner;
  std::uint32_t poll_events;
  std::int32_t backing_id;
  std::uint64_t compat_flags;
  std::uint64_t reserved[2];
};

// The lowlevel operation table, in libfuse's field order, truncated after the last op xff
// implements (`readdir`). libfuse zero-fills whatever lies past the `op_size` we pass, so the ops we
// leave out simply do not exist for our session - but the ones we DO declare have to sit at exactly
// their upstream offsets, which is why the unimplemented ones in between are still declared.
struct fuse_lowlevel_ops {
  void (*init)(void* userdata, struct fuse_conn_info* conn);
  void (*destroy)(void* userdata);
  void (*lookup)(fuse_req_t req, fuse_ino_t parent, const char* name);
  void (*forget)(fuse_req_t req, fuse_ino_t ino, std::uint64_t nlookup);
  void (*getattr)(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);
  void (*setattr)(fuse_req_t req, fuse_ino_t ino, struct stat* attr, int to_set, struct fuse_file_info* fi);
  void (*readlink)(fuse_req_t req, fuse_ino_t ino);
  void (*mknod)(fuse_req_t req, fuse_ino_t parent, const char* name, mode_t mode, dev_t rdev);
  void (*mkdir)(fuse_req_t req, fuse_ino_t parent, const char* name, mode_t mode);
  void (*unlink)(fuse_req_t req, fuse_ino_t parent, const char* name);
  void (*rmdir)(fuse_req_t req, fuse_ino_t parent, const char* name);
  void (*symlink)(fuse_req_t req, const char* link, fuse_ino_t parent, const char* name);
  void (*rename)(
      fuse_req_t req,
      fuse_ino_t parent,
      const char* name,
      fuse_ino_t newparent,
      const char* newname,
      unsigned int flags);
  void (*link)(fuse_req_t req, fuse_ino_t ino, fuse_ino_t newparent, const char* newname);
  void (*open)(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);
  void (*read)(fuse_req_t req, fuse_ino_t ino, std::size_t size, off_t off, struct fuse_file_info* fi);
  void (*write)(
      fuse_req_t req,
      fuse_ino_t ino,
      const char* buf,
      std::size_t size,
      off_t off,
      struct fuse_file_info* fi);
  void (*flush)(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);
  void (*release)(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);
  void (*fsync)(fuse_req_t req, fuse_ino_t ino, int datasync, struct fuse_file_info* fi);
  void (*opendir)(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);
  void (*readdir)(fuse_req_t req, fuse_ino_t ino, std::size_t size, off_t off, struct fuse_file_info* fi);
};

}  // extern "C"

#endif  // XFF_FUSE_FUSE_ABI_H_
