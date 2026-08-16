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

// The FUSE extra's identity: linking this translation unit IS what "the fuse extra is compiled in"
// means (epic #183 slice 4a). It fills the xff_extras_api slot the core reads for `--help=extras` /
// the notice line, and registers the libfuse notice. The target is alwayslink so neither file-scope
// registrar is dropped.

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "mbo/status/status_macros.h"
#include "xff/fuse/fuse_backend.h"
#include "xff/fuse/fuse_server.h"
#include "xff/fuse/mount_root.h"
#include "xff/license/notice.h"
#include "xff/vfs/filesystem.h"

namespace xff::fuse {
namespace {

// The run's mount-point tree, created on first use and removed when the process ends. One root per
// RUN (not per mount) is what makes a crash recoverable: the pid in its path is how a later run
// tells an abandoned tree from a live one.
absl::StatusOr<MountRoot>& RunRoot() {
  static absl::StatusOr<MountRoot>& root = *new absl::StatusOr<MountRoot>(MountRoot::Create());
  return root;
}

absl::Mutex& RootMutex() {
  static absl::Mutex& mutex = *new absl::Mutex();
  return mutex;
}

// A mounted container: the server plus the mount point, answering member paths.
class ServerMount final : public Mount {
 public:
  ServerMount(std::unique_ptr<FuseServer> server, std::string container)
      : server_(std::move(server)), container_(std::move(container)) {}

  [[nodiscard]] std::string_view MountPoint() const override { return server_->MountPoint(); }

  [[nodiscard]] std::string PathFor(std::string_view member) const override {
    return absl::StrCat(server_->MountPoint(), "/", member);
  }

 private:
  std::unique_ptr<FuseServer> server_;
  std::string container_;
};

absl::StatusOr<std::unique_ptr<Mount>> MountThroughFuse(const vfs::FileSystem& fs, std::string_view container) {
  std::string mount_point;
  {
    const absl::MutexLock lock(RootMutex());
    MBO_RETURN_IF_ERROR(RunRoot().status());
    MBO_ASSIGN_OR_RETURN(mount_point, RunRoot()->MountPointFor(container));
  }
  MBO_ASSIGN_OR_RETURN(std::unique_ptr<FuseServer> server, FuseServer::Mount(fs, std::string(container), mount_point));
  return std::make_unique<ServerMount>(std::move(server), std::string(container));
}

const MountSupportRegistrar kRegisterFuseMount{};

// Registering the factory is what makes MountContainer work; the slot above is only the "is it
// linked" answer the help and notice surfaces read.
const bool kRegisterFactory = [] {
  RegisterMountFactory(&MountThroughFuse);
  return true;
}();

// Interface-only use: the binary compiles against libfuse's headers (fetched, pinned release) and
// dlopens the SYSTEM's libfuse3 at runtime; no LGPL code is compiled or statically linked in. The
// notice credits the interface and says where the library itself lives.
const license::Registrar kLibfuseNotice{{
    .component = "libfuse",
    .spdx = "LGPL-2.1-only",
    .text = "libfuse (https://github.com/libfuse/libfuse), Copyright (c) Miklos Szeredi and contributors.\n"
            "Used interface-only: compiled against its headers; the library itself is loaded from the\n"
            "host system at runtime (dlopen) and is not distributed with this binary.",
}};

}  // namespace
}  // namespace xff::fuse
