// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ranges>
#include <string_view>

#include "absl/status/status.h"
#include "xff/archive/phar_reader.h"

namespace {

constexpr std::size_t kMaxInputBytes = 16UZ * 1'024 * 1'024;
constexpr std::size_t kMaxMembersRead = 32;
constexpr std::uint64_t kMaxMemberBytes = 1UZ * 1'024 * 1'024;

void CheckRange(std::size_t offset, std::uint64_t length, std::size_t size) {
  if (offset > size || length > size - offset) {
    std::abort();
  }
}

void CheckLayout(const xff::archive::PharLayout& layout, std::size_t size) {
  CheckRange(layout.manifest_length_at, 4, size);
  CheckRange(layout.manifest_start, layout.manifest_size, size);
  if (layout.entries_offset < layout.manifest_start || layout.entries_offset > layout.data_offset
      || layout.data_offset > layout.data_end || layout.data_end > size) {
    std::abort();
  }
  for (const xff::archive::PharMemberLayout& member : layout.members) {
    if (member.entry_offset < layout.manifest_start) {
      std::abort();
    }
    CheckRange(member.entry_offset, member.entry_size, layout.data_offset);
    CheckRange(static_cast<std::size_t>(member.data_offset), member.stored_size, layout.data_end);
  }
}

}  // namespace

// XFF_ABI_POINTER: rules_fuzzing requires libFuzzer's C entry-point signature.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > kMaxInputBytes) {
    return 0;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const std::string_view bytes(reinterpret_cast<const char*>(data), size);
  const auto layout = xff::archive::ParsePharLayout(bytes);
  if (!layout.ok()) {
    return 0;
  }
  CheckLayout(*layout, size);

  const auto members = xff::archive::ListPharMembers(bytes);
  if (!members.ok() || members->size() < layout->members.size()) {
    std::abort();
  }
  for (const auto& member : *members | std::views::take(kMaxMembersRead)) {
    const auto content = xff::archive::ReadPharMember(bytes, member.path, kMaxMemberBytes);
    if (!content.ok() && content.status().code() != absl::StatusCode::kFailedPrecondition
        && content.status().code() != absl::StatusCode::kResourceExhausted
        && content.status().code() != absl::StatusCode::kDataLoss
        && content.status().code() != absl::StatusCode::kUnimplemented) {
      std::abort();
    }
  }
  return 0;
}
