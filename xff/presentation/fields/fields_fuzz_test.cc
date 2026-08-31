// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/time/time.h"
#include "xff/presentation/fields/fields.h"
#include "xff/vfs/entry.h"

namespace {

struct Input {
  std::string_view tmpl;
  std::string_view path;
  std::string_view captured;
};

Input DecodeInput(std::string_view input) {
  const std::size_t first = input.find('\n');
  if (first == std::string_view::npos) {
    return {.tmpl = input};
  }
  const std::size_t second = input.find('\n', first + 1);
  if (second == std::string_view::npos) {
    return {.tmpl = input.substr(0, first), .path = input.substr(first + 1)};
  }
  return {
      .tmpl = input.substr(0, first),
      .path = input.substr(first + 1, second - first - 1),
      .captured = input.substr(second + 1),
  };
}

}  // namespace

// XFF_ABI_POINTER: rules_fuzzing requires libFuzzer's C entry-point signature.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  // libFuzzer exposes object-representation bytes; template literals and paths may contain arbitrary
  // non-NUL bytes, while malformed field syntax must remain safe too.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const Input input = DecodeInput(std::string_view(reinterpret_cast<const char*>(data), size));

  xff::vfs::Metadata metadata;
  metadata.type = xff::vfs::FileType::kRegular;
  metadata.size = input.captured.size();
  metadata.atime = absl::UnixEpoch();
  metadata.mtime = absl::UnixEpoch();
  metadata.ctime = absl::UnixEpoch();
  metadata.btime = absl::UnixEpoch();
  const std::vector<std::string> captures = {std::string(input.captured)};
  const std::map<std::string, std::string> values = {{"value", std::string(input.captured)}};
  const std::string path = "/xff-fuzz-nonexistent/" + std::string(input.path);
  const xff::fields::RenderContext context{
      .path = path,
      .root = "/xff-fuzz-nonexistent",
      .metadata = metadata,
      .depth = 1,
      .tz = absl::UTCTimeZone(),
      .captures = captures,
      .defines = values,
      .outputs = values,
      .line_number = 1,
      .line_text = input.captured,
      .match_text = input.captured,
      .match_column = 1,
      .shard_count = 1,
      .fuzzy_score = 100,
  };

  const xff::fields::Template compiled = xff::fields::Template::Compile(input.tmpl);
  const std::string rendered = compiled.Render(context);
  const std::optional<std::vector<std::string>> extraction = compiled.AsExtraction(context);
  if (compiled.IsExtraction() != extraction.has_value()) {
    std::abort();
  }
  if (compiled.IsExtraction() && !compiled.HasUnreducedExtraction()) {
    std::abort();
  }
  if (rendered != compiled.Render(context)) {
    std::abort();
  }
  return 0;
}
