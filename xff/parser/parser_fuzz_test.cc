// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "xff/parser/parser.h"
#include "xff/registry/descriptor.h"

namespace {

constexpr std::size_t kMaxInputBytes = 16 * 1'024;
constexpr auto kStyles = std::to_array<xff::registry::Style>({
    xff::registry::Style::kFind,
    xff::registry::Style::kXff,
    xff::registry::Style::kRg,
});

std::vector<std::string> DecodeArgs(std::string_view input) {
  std::vector<std::string> args;
  while (!input.empty()) {
    const std::size_t separator = input.find('\n');
    if (separator == std::string_view::npos) {
      args.emplace_back(input);
      break;
    }
    args.emplace_back(input.substr(0, separator));
    input.remove_prefix(separator + 1);
  }
  return args;
}

}  // namespace

// XFF_ABI_POINTER: rules_fuzzing requires libFuzzer's C entry-point signature.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > kMaxInputBytes) {
    return 0;
  }
  // libFuzzer exposes object-representation bytes; the parser consumes the same bytes as argv text.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  const auto parsed = xff::parser::Parse(DecodeArgs(input));
  if (!parsed.ok()) {
    return 0;
  }

  for (const xff::registry::Style style : kStyles) {
    (void)xff::parser::EnforceStyle(*parsed, style);
    (void)xff::parser::ResolveCaseMode(parsed->globals, style);
  }
  (void)xff::parser::TakesTerminal(*parsed);
  return 0;
}
