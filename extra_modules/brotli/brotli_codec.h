// SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com)
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_BROTLI_BROTLI_CODEC_H_
#define XFF_BROTLI_BROTLI_CODEC_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xff/archive/archive_backend.h"

namespace xff::brotli {

[[nodiscard]] absl::StatusOr<std::string> Decode(
    std::string_view path,
    std::optional<std::string_view> bytes,
    std::uint64_t max_bytes = 0);

[[nodiscard]] absl::Status PackTar(
    std::string_view path,
    const std::vector<archive::PackFile>& files,
    const archive::PackOptions& options);

}  // namespace xff::brotli

#endif  // XFF_BROTLI_BROTLI_CODEC_H_
