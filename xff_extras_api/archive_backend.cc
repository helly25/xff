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

#include "xff/archive/archive_backend.h"

#include <memory>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xff/archive/member_path.h"
#include "xff/vfs/filesystem.h"

namespace xff::archive {
namespace {

// The process-wide container opener, empty when no archive backend is linked. Set once at static init
// by the real backend's ContainerRegistrar (full build only); a Meyers static so a registrar in
// another translation unit can safely write it during static initialization.
ContainerOpener& ContainerOpenerSlot() {
  static ContainerOpener slot;
  return slot;
}

}  // namespace

void RegisterContainerOpener(ContainerOpener opener) {
  ContainerOpenerSlot() = std::move(opener);
}

bool ContainerSupportAvailable() {
  return static_cast<bool>(ContainerOpenerSlot());
}

absl::StatusOr<std::unique_ptr<vfs::FileSystem>> OpenContainer(std::string_view container, MemberPathOptions options) {
  if (!ContainerSupportAvailable()) {
    // Unimplemented, not InvalidArgument: nothing is wrong with the path, this binary simply cannot
    // look inside it. The CLI turns this into the "not built into this binary" message.
    return absl::UnimplementedError("this binary was built without archive support");
  }
  return ContainerOpenerSlot()(container, options);
}

}  // namespace xff::archive
