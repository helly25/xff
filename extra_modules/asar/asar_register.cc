// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/asar/asar_register.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "mbo/status/status_macros.h"
#include "xff/archive/archive_backend.h"
#include "xff/archive/member_path.h"
#include "xff/asar/asar_fs.h"
#include "xff/license/notice.h"
#include "xff/vfs/filesystem.h"

namespace xff::asar {
namespace {

absl::StatusOr<std::unique_ptr<vfs::FileSystem>> OpenAsarContainer(
    std::string_view container,
    std::optional<std::string_view> bytes,
    archive::MemberPathOptions options) {
  MBO_ASSIGN_OR_RETURN(
      AsarFileSystem fs, bytes.has_value() ? AsarFileSystem::OpenBytes(container, std::string(*bytes), options)
                                           : AsarFileSystem::Open(container, options));
  return std::make_unique<AsarFileSystem>(std::move(fs));
}

const license::Registrar kExtensionNotice{{
    .section = "Electron ASAR (@xff_asar)",
    .section_lead = true,
    .component = "xff Electron ASAR extra (@xff_asar)",
    .spdx = "Apache-2.0",
    .text = "Copyright M. Boerger and the MBO Works authors. Licensed under the Apache License, Version 2.0.",
}};

// NOLINTNEXTLINE(fuchsia-statically-constructed-objects,cert-err58-cpp)
const struct AsarRegistrar {
  AsarRegistrar() { RegisterAsarBackend(); }
} kRegisterAsar;

}  // namespace

void RegisterAsarBackend() {
  archive::RegisterContainerReader(
      "asar", &OpenAsarContainer, {{.name = "asar", .suffixes = {".asar"}, .detail = "Electron application archives"}});
}

}  // namespace xff::asar
