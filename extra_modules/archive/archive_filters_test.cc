// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "xff/archive/archive_filters.h"

#include <archive.h>

#include <memory>
#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::archive {
namespace {

using ::testing::ElementsAre;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Optional;
using ::testing::ResultOf;

int RejectLz4(struct ::archive* handle, const int code) {
  return code == ARCHIVE_FILTER_LZ4 ? ARCHIVE_FATAL : ::archive_read_support_filter_by_code(handle, code);
}

struct ArchiveFiltersTest : ::testing::Test {};

TEST_F(ArchiveFiltersTest, AllowlistContainsOnlyStandaloneNativeFilters) {
  EXPECT_THAT(
      NativeFilterCodes(), ElementsAre(
                               ARCHIVE_FILTER_NONE, ARCHIVE_FILTER_GZIP, ARCHIVE_FILTER_BZIP2, ARCHIVE_FILTER_COMPRESS,
                               ARCHIVE_FILTER_LZMA, ARCHIVE_FILTER_XZ, ARCHIVE_FILTER_UU, ARCHIVE_FILTER_RPM,
                               ARCHIVE_FILTER_LZIP, ARCHIVE_FILTER_LZ4, ARCHIVE_FILTER_ZSTD));
}

TEST_F(ArchiveFiltersTest, RegistrationEnablesEveryPromisedFilter) {
  const std::unique_ptr<struct ::archive, decltype(&::archive_read_free)> handle(
      ::archive_read_new(), ::archive_read_free);
  const auto enable = [](const auto& reader) -> std::optional<bool> {
    return reader ? std::optional<bool>{EnableNativeFilters(*reader)} : std::nullopt;
  };
  EXPECT_THAT(handle, ResultOf("EnableNativeFilters", enable, Optional(IsTrue())));
}

TEST_F(ArchiveFiltersTest, RegistrationFailsInsteadOfLeavingAPartialAllowlist) {
  const std::unique_ptr<struct ::archive, decltype(&::archive_read_free)> handle(
      ::archive_read_new(), ::archive_read_free);
  const auto enable_without_lz4 = [](const auto& reader) -> std::optional<bool> {
    return reader ? std::optional<bool>{internal::EnableNativeFiltersWith(*reader, RejectLz4)} : std::nullopt;
  };
  EXPECT_THAT(handle, ResultOf("EnableNativeFiltersWith", enable_without_lz4, Optional(IsFalse())));
}

}  // namespace
}  // namespace xff::archive
