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

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
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

// The process-wide member remover, empty when no backend registered one - which is the answer for a
// build without archive support AND for a backend that can only read.
ContainerMemberRemover& ContainerMemberRemoverSlot() {
  static ContainerMemberRemover slot;
  return slot;
}

}  // namespace

void RegisterContainerOpener(ContainerOpener opener) {
  ContainerOpenerSlot() = std::move(opener);
}

void RegisterContainerMemberRemover(ContainerMemberRemover remover) {
  ContainerMemberRemoverSlot() = std::move(remover);
}

bool ContainerRemovalAvailable() {
  return static_cast<bool>(ContainerMemberRemoverSlot());
}

absl::Status RemoveContainerMembers(std::string_view container, const std::vector<std::string>& members) {
  if (!ContainerRemovalAvailable()) {
    return absl::UnimplementedError("this binary was built without archive support");
  }
  return ContainerMemberRemoverSlot()(container, members);
}

// Every suffix the reader has a format or filter for, plus the package extensions that are one of
// those underneath (a `.jar` is a zip, a `.crate` a tar.gz). Lower case; the comparison folds, so a
// shouted `ARCHIVE.ZIP` matches too. Compound suffixes (`.tar.gz`) need no entry: their last
// component (`.gz`) is already here.
constexpr std::array kContainerSuffixes = std::to_array<std::string_view>({
    ".7z",   ".aab",  ".apk",  ".ar",   ".bz2", ".cab",  ".cbz", ".crate", ".crx", ".deb",  ".ear",   ".egg",
    ".epub", ".gem",  ".gz",   ".iso",  ".jar", ".jmod", ".lha", ".lz4",   ".lzh", ".lzma", ".nupkg", ".odp",
    ".ods",  ".odt",  ".phar", ".pptx", ".rar", ".rpm",  ".tar", ".taz",   ".tbz", ".tbz2", ".tgz",   ".txz",
    ".tz2",  ".vsix", ".war",  ".whl",  ".xar", ".xpi",  ".xz",  ".zip",   ".zst", ".zstd",
});

bool ContainerSupportAvailable() {
  return static_cast<bool>(ContainerOpenerSlot());
}

bool LooksLikeContainerName(std::string_view name) {
  const std::string::size_type dot = name.rfind('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 == name.size()) {
    // No suffix at all (a Makefile, a compiled binary), a trailing dot, or a name that IS the suffix:
    // `.gz` is a dotfile, not a gzip called something.
    return false;
  }
  const std::string_view suffix = name.substr(dot);
  return absl::c_any_of(
      kContainerSuffixes, [suffix](std::string_view known) { return absl::EqualsIgnoreCase(suffix, known); });
}

absl::StatusOr<std::unique_ptr<vfs::FileSystem>> OpenContainer(std::string_view container, MemberPathOptions options) {
  if (!ContainerSupportAvailable()) {
    // Unimplemented, not InvalidArgument: nothing is wrong with the path, this binary simply cannot
    // look inside it. The CLI turns this into the "not built into this binary" message.
    return absl::UnimplementedError("this binary was built without archive support");
  }
  return ContainerOpenerSlot()(container, std::nullopt, options);
}

absl::StatusOr<std::unique_ptr<vfs::FileSystem>> OpenContainerBytes(
    std::string_view container,
    std::string_view bytes,
    MemberPathOptions options) {
  if (!ContainerSupportAvailable()) {
    return absl::UnimplementedError("this binary was built without archive support");
  }
  return ContainerOpenerSlot()(container, bytes, options);
}

}  // namespace xff::archive
