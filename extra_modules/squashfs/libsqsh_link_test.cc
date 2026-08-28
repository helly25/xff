// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#include <sqsh.h>

#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "libsqsh_codec_probe.h"

namespace xff::squashfs {
namespace {

using ::testing::HasSubstr;
using ::testing::IsFalse;
using ::testing::IsTrue;

struct LibsqshLinkTest : ::testing::Test {};

TEST_F(LibsqshLinkTest, ExposesVersionedLibrary) {
  EXPECT_THAT(std::string_view(sqsh_version()), HasSubstr("1.5.2"));
}

TEST_F(LibsqshLinkTest, LinksEveryPermissivelyLicensedSquashfsCodecButNotLzo) {
  EXPECT_THAT(xff_libsqsh_has_extractor(SQSH_COMPRESSION_GZIP), IsTrue());
  EXPECT_THAT(xff_libsqsh_has_extractor(SQSH_COMPRESSION_LZMA), IsTrue());
  EXPECT_THAT(xff_libsqsh_has_extractor(SQSH_COMPRESSION_XZ), IsTrue());
  EXPECT_THAT(xff_libsqsh_has_extractor(SQSH_COMPRESSION_LZ4), IsTrue());
  EXPECT_THAT(xff_libsqsh_has_extractor(SQSH_COMPRESSION_ZSTD), IsTrue());
  EXPECT_THAT(xff_libsqsh_has_extractor(SQSH_COMPRESSION_LZO), IsFalse());
}

}  // namespace
}  // namespace xff::squashfs
