// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
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

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::archive {
namespace {

using ::testing::ElementsAre;
using ::testing::IsFalse;
using ::testing::IsTrue;

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

TEST_F(ArchiveFiltersTest, RegistrationRequiresAReaderAndEveryPromisedFilter) {
  EXPECT_THAT(EnableNativeFilters(nullptr), IsFalse());
  struct ::archive* const handle = ::archive_read_new();
  ASSERT_THAT(handle != nullptr, IsTrue());
  EXPECT_THAT(EnableNativeFilters(handle), IsTrue());
  EXPECT_THAT(::archive_read_free(handle), 0);
}

TEST_F(ArchiveFiltersTest, RegistrationFailsInsteadOfLeavingAPartialAllowlist) {
  struct ::archive* const handle = ::archive_read_new();
  ASSERT_THAT(handle != nullptr, IsTrue());
  EXPECT_THAT(internal::EnableNativeFiltersWith(handle, RejectLz4), IsFalse());
  EXPECT_THAT(::archive_read_free(handle), 0);
}

}  // namespace
}  // namespace xff::archive
