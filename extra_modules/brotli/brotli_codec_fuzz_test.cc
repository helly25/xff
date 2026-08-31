// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>

#include "xff/brotli/brotli_codec.h"

namespace {

constexpr std::size_t kMaxInputBytes = 4UZ * 1'024 * 1'024;
constexpr std::uint64_t kMaxDecodedBytes = 1UZ * 1'024 * 1'024;

void CheckDeterministic(const absl::StatusOr<std::string>& first, const absl::StatusOr<std::string>& second) {
  if (first.ok() != second.ok()) {
    std::abort();
  }
  if (first.ok()) {
    if (*first != *second || first->size() > kMaxDecodedBytes) {
      std::abort();
    }
  } else if (first.status().code() != second.status().code()) {
    std::abort();
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
  const auto first = xff::brotli::Decode("fuzz.tar.br", std::optional<std::string_view>{bytes}, kMaxDecodedBytes);
  const auto second = xff::brotli::Decode("fuzz.tar.br", std::optional<std::string_view>{bytes}, kMaxDecodedBytes);
  CheckDeterministic(first, second);
  return 0;
}
