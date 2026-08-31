// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_LANGUAGE_DB_LANGUAGE_DATABASE_H_
#define XFF_LANGUAGE_DB_LANGUAGE_DATABASE_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "absl/status/statusor.h"

namespace xff::language_db {

// Raw Brotli does not expose its decoded size up front. The generated expected size provides the
// output capacity required by the one-shot decoder and verifies the embedded payload after decoding.
absl::StatusOr<std::string> Decode(std::span<const std::uint8_t> compressed, std::size_t expected_uncompressed_size);

}  // namespace xff::language_db

#endif  // XFF_LANGUAGE_DB_LANGUAGE_DATABASE_H_
