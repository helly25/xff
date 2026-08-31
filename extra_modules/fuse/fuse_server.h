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

#ifndef XFF_FUSE_FUSE_SERVER_H_
#define XFF_FUSE_FUSE_SERVER_H_

// The read-only FUSE server (epic #183, slice 3b): one mounted view of one `vfs::FileSystem`, so
// every external tool - `-exec` children first of all - gets a REAL path into a container with
// nothing extracted. Serves lookup/getattr/readdir/open/read/readlink from the filesystem the
// walk already opened; writes are refused with EROFS by construction (no write callbacks exist).
//
// Lifecycle: `Mount` starts one background thread running the fuse session loop; the destructor
// exits the loop, joins, unmounts and destroys the session (RAII). INT/TERM/HUP additionally ask
// every live session to exit before the default handler re-raises, so a Ctrl-C never strands a
// mount (the per-pid mount root and the stale sweep in mount_root.h are the crash net under that).
//
// Requires a runtime fuse3 (FuseApi::Resolve); where the machine has none, Mount returns
// Unavailable with the loader's reason and the caller degrades to extraction.

#include <memory>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "xff/vfs/filesystem.h"

namespace xff::fuse {

class FuseServer {
 public:
  // Mounts `fs` at `mount_point` (an existing empty directory, normally from MountRoot) with the
  // mount's "/" mapping to `root` inside the filesystem (the container path). The server KEEPS its
  // share of `fs`: callbacks run on the serving thread and read it until the destructor joins, so
  // the server's own lifetime is the only correct bound. Unavailable when the machine has no
  // fuse3; any mount failure reports why.
  static absl::StatusOr<std::unique_ptr<FuseServer>> Mount(
      std::shared_ptr<const vfs::FileSystem> fs,
      std::string root,
      std::string mount_point);

  // Exits the loop, joins the serving thread, unmounts, destroys the session.
  ~FuseServer();

  FuseServer(const FuseServer&) = delete;
  FuseServer& operator=(const FuseServer&) = delete;
  FuseServer(FuseServer&&) = delete;
  FuseServer& operator=(FuseServer&&) = delete;

  // Where this server is mounted; what `{}` renders as for a member under it.
  [[nodiscard]] std::string_view MountPoint() const;

  // The per-mount state (session, inode table, serving thread). Public only in NAME: it is defined
  // in the .cc, where the C callbacks - plain functions with a void* userdata slot - must be able
  // to spell the type; no caller can do anything with it.
  struct Impl;

 private:
  FuseServer();
  std::unique_ptr<Impl> impl_;
};

// Best-effort unmount of an abandoned mount point by pid-less means: `fusermount3 -uz` on Linux,
// `umount -f` elsewhere. This is the unmounter SweepStaleRoots wants for roots a crashed run left
// behind; failures are silent by design (a busy mount stays for the next sweep).
void CrashUnmount(std::string_view mount_point);

}  // namespace xff::fuse

#endif  // XFF_FUSE_FUSE_SERVER_H_
