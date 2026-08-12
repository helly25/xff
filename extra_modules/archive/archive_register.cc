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

// Plugs this extra into the core's archive seam. Linking this target is the ONLY thing that makes
// `--archive` able to look inside a container; a build without it reports no archive support.
//
// Kept in its own translation unit, exactly like the PCRE2 backend's registrar: the registration is a
// static-init side effect in an `alwayslink` target, so the linker cannot drop it, and separating it
// keeps `archive_fs_cc` itself free of global state (a test can construct an ArchiveFileSystem without
// the process-wide slot being touched).

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/statusor.h"
#include "mbo/status/status_macros.h"
#include "xff/archive/archive_backend.h"
#include "xff/archive/archive_fs.h"
#include "xff/archive/member_path.h"
#include "xff/vfs/filesystem.h"

namespace xff::archive {
namespace {

// Adapts ArchiveFileSystem::Open (which returns the filesystem BY VALUE, so it stays usable without
// heap allocation in its own tests) to the seam's owning-pointer contract. The reader's status is
// returned unchanged, which is what keeps "not an archive" (InvalidArgument) apart from "corrupt
// archive" (DataLoss) all the way out to the walk.
absl::StatusOr<std::unique_ptr<vfs::FileSystem>> OpenArchiveContainer(
    std::string_view container,
    std::optional<std::string_view> bytes,
    MemberPathOptions options) {
  // With bytes, the container is nested and `container` is only the label its members render under;
  // without them it is a real path. Both index identically once opened.
  MBO_ASSIGN_OR_RETURN(
      ArchiveFileSystem archive_fs, bytes.has_value()
                                        ? ArchiveFileSystem::OpenBytes(container, std::string(*bytes), options)
                                        : ArchiveFileSystem::Open(container, options));
  return std::make_unique<ArchiveFileSystem>(std::move(archive_fs));
}

const ContainerRegistrar kRegisterArchiveContainer{&OpenArchiveContainer};

}  // namespace
}  // namespace xff::archive
