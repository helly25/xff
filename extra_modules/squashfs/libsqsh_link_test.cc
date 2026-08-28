// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#include <sqsh.h>
#include <sqsh_extract_private.h>

#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::squashfs {
namespace {

using ::testing::HasSubstr;
using ::testing::IsNull;
using ::testing::NotNull;

struct LibsqshLinkTest : ::testing::Test {};

TEST_F(LibsqshLinkTest, ExposesVersionedLibrary) {
  EXPECT_THAT(std::string_view(sqsh_version()), HasSubstr("1.5.2"));
}

TEST_F(LibsqshLinkTest, LinksEveryPermissivelyLicensedSquashfsCodecButNotLzo) {
  EXPECT_THAT(sqsh__extractor_impl_from_id(SQSH_COMPRESSION_GZIP), NotNull());
  EXPECT_THAT(sqsh__extractor_impl_from_id(SQSH_COMPRESSION_LZMA), NotNull());
  EXPECT_THAT(sqsh__extractor_impl_from_id(SQSH_COMPRESSION_XZ), NotNull());
  EXPECT_THAT(sqsh__extractor_impl_from_id(SQSH_COMPRESSION_LZ4), NotNull());
  EXPECT_THAT(sqsh__extractor_impl_from_id(SQSH_COMPRESSION_ZSTD), NotNull());
  EXPECT_THAT(sqsh__extractor_impl_from_id(SQSH_COMPRESSION_LZO), IsNull());
}

}  // namespace
}  // namespace xff::squashfs
