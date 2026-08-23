// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#include "language_database.h"

#include <brotli/decode.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "language_db_data_cc_generated.h"
#include "xff/matching/language/language_database_api.h"

namespace xff::language_db {

absl::StatusOr<std::string> Decode(std::span<const std::uint8_t> compressed, std::size_t expected_uncompressed_size) {
  std::vector<std::uint8_t> result(expected_uncompressed_size);
  std::size_t size = result.size();
  const BrotliDecoderResult decode =
      BrotliDecoderDecompress(compressed.size(), compressed.data(), &size, result.data());
  if (decode != BROTLI_DECODER_RESULT_SUCCESS) {
    return absl::DataLossError("the embedded language database is not valid Brotli data");
  }
  if (size != expected_uncompressed_size) {
    return absl::DataLossError(
        absl::StrCat(
            "the embedded language database decoded to ", size, " bytes, expected ", expected_uncompressed_size));
  }
  return std::string(result.begin(), result.end());
}

namespace {

std::string_view Json() {
  static const std::string kJson = [] {
    const absl::StatusOr<std::string> json = Decode(data::Compressed(), data::UncompressedSize());
    CHECK_OK(json);
    return *json;
  }();
  return kJson;
}

const language::DatabaseRegistrar kDatabase{{
    .name = "github-linguist 9.6.0",
    .json = &Json,
}};

}  // namespace
}  // namespace xff::language_db
