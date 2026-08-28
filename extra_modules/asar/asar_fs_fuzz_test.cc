// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ranges>
#include <string>
#include <string_view>

#include "xff/archive/member_path.h"
#include "xff/asar/asar_fs.h"
#include "xff/vfs/entry.h"

namespace {

constexpr std::size_t kMaxInputBytes = 4UZ * 1'024 * 1'024;
constexpr std::size_t kMaxEntries = 64;
constexpr std::string_view kContainer = "fuzz.asar";

void AppendU32(std::string& output, std::uint32_t value) {
  output.push_back(static_cast<char>(value & 0xffU));
  output.push_back(static_cast<char>((value >> 8U) & 0xffU));
  output.push_back(static_cast<char>((value >> 16U) & 0xffU));
  output.push_back(static_cast<char>((value >> 24U) & 0xffU));
}

std::string WrapJson(std::string_view json) {
  std::string header_payload;
  AppendU32(header_payload, static_cast<std::uint32_t>(json.size()));
  header_payload.append(json);
  while (header_payload.size() % 4 != 0) {
    header_payload.push_back('\0');
  }
  std::string header_pickle;
  AppendU32(header_pickle, static_cast<std::uint32_t>(header_payload.size()));
  header_pickle.append(header_payload);
  std::string archive;
  AppendU32(archive, 4);
  AppendU32(archive, static_cast<std::uint32_t>(header_pickle.size()));
  archive.append(header_pickle);
  return archive;
}

void CheckTree(const xff::asar::AsarFileSystem& asar, std::string_view directory) {
  const auto entries = asar.ReadDir(directory);
  if (!entries.ok()) {
    std::abort();
  }
  for (const xff::vfs::Entry& entry : *entries | std::views::take(kMaxEntries)) {
    const std::string path = xff::archive::JoinMemberPath(directory, entry.name);
    const auto metadata = asar.Stat(path, false);
    if (!metadata.ok() || metadata->type != entry.type) {
      std::abort();
    }
    if (entry.type == xff::vfs::FileType::kDirectory) {
      if (!asar.ReadDir(path).ok()) {
        std::abort();
      }
    } else if (entry.type == xff::vfs::FileType::kSymlink) {
      if (!asar.ReadLink(path).ok()) {
        std::abort();
      }
    } else {
      (void)asar.ReadContent(path);
    }
  }
}

}  // namespace

// XFF_ABI_POINTER: rules_fuzzing requires libFuzzer's C entry-point signature.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > kMaxInputBytes) {
    return 0;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  std::string bytes = input.starts_with('{') ? WrapJson(input) : std::string(input);
  const auto asar = xff::asar::AsarFileSystem::OpenBytes(kContainer, std::move(bytes));
  if (asar.ok()) {
    CheckTree(*asar, kContainer);
  }
  return 0;
}
