// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ARCHIVE_ARCHIVE_REGISTER_H_
#define XFF_ARCHIVE_ARCHIVE_REGISTER_H_

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xff/archive/archive_backend.h"
#include "xff/archive/member_path.h"
#include "xff/vfs/filesystem.h"

namespace xff::archive {

// Installs or refreshes the aggregate archive backend. A dependent compression extra calls this
// after registering itself; doing so makes static-initialization order irrelevant.
void RegisterArchiveBackend();

[[nodiscard]] absl::StatusOr<std::unique_ptr<vfs::FileSystem>> OpenArchiveContainer(
    std::string_view container,
    std::optional<std::string_view> bytes,
    MemberPathOptions options);
[[nodiscard]] absl::Status PackNativeArchiveContainer(
    std::string_view path,
    const std::vector<PackFile>& files,
    const PackOptions& options);

}  // namespace xff::archive

#endif  // XFF_ARCHIVE_ARCHIVE_REGISTER_H_
