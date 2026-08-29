// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <ranges>
#include <string_view>

#include "xff/archive/archive_reader.h"

namespace {

constexpr std::size_t kMaxInputBytes = 64UZ * 1'024;
constexpr std::size_t kMaxMembersRead = 32;
constexpr std::uint64_t kMaxMemberBytes = 1UZ * 1'024 * 1'024;

bool LooksLikeTar(std::string_view bytes) {
  if (bytes.size() < 512 || bytes.substr(257, 5) != "ustar") {
    return false;
  }
  const auto parse_octal = [](std::string_view field) -> std::uint64_t {
    std::uint64_t value = 0;
    bool digit = false;
    for (const char byte : field) {
      if (byte == ' ' || byte == '\0') {
        continue;
      }
      if (byte < '0' || byte > '7' || value > std::numeric_limits<std::uint64_t>::max() / 8) {
        return std::numeric_limits<std::uint64_t>::max();
      }
      value = (value * 8) + static_cast<std::uint64_t>(byte - '0');
      digit = true;
    }
    return digit ? value : std::numeric_limits<std::uint64_t>::max();
  };
  const std::string_view header = bytes.substr(0, 512);
  const std::uint64_t stored = parse_octal(header.substr(148, 8));
  if (stored == std::numeric_limits<std::uint64_t>::max()) {
    return false;
  }
  std::uint64_t computed = 0;
  for (std::size_t index = 0; index < header.size(); ++index) {
    computed +=
        index >= 148 && index < 156 ? static_cast<unsigned char>(' ') : static_cast<unsigned char>(header[index]);
  }
  return computed == stored;
}
}  // namespace

// XFF_ABI_POINTER: rules_fuzzing requires libFuzzer's C entry-point signature.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > kMaxInputBytes) {
    return 0;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const std::string_view bytes(reinterpret_cast<const char*>(data), size);
  if (!LooksLikeTar(bytes)) {
    return 0;
  }
  const auto members = xff::archive::ListMembers(bytes);
  if (!members.ok()) {
    return 0;
  }
  for (const auto& member : *members | std::views::take(kMaxMembersRead)) {
    const auto content = xff::archive::ReadMember(bytes, member.path, kMaxMemberBytes);
    if (content.ok() && content->size() > kMaxMemberBytes) {
      std::abort();
    }
  }
  return 0;
}
