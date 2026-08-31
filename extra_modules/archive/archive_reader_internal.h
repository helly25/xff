// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ARCHIVE_ARCHIVE_READER_INTERNAL_H_
#define XFF_ARCHIVE_ARCHIVE_READER_INTERNAL_H_

#include <archive.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "xff/archive/archive_reader.h"

namespace xff::archive::internal {

using FilterEnabler = bool (*)(struct ::archive&);

// Dependency-injected forms used to prove that filter registration failures become stable API
// errors. Production entry points pass EnableNativeFilters.
absl::StatusOr<std::vector<Member>> ListMembersWithFilterEnabler(std::string_view bytes, FilterEnabler enable_filters);
absl::StatusOr<std::string> ReadCompressedSingleFileWithFilterEnabler(
    std::string_view path,
    std::uint64_t max_bytes,
    FilterEnabler enable_filters);

}  // namespace xff::archive::internal

#endif  // XFF_ARCHIVE_ARCHIVE_READER_INTERNAL_H_
