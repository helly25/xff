// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/license/notice.h"

namespace xff::squashfs {
namespace {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Field;

struct SquashfsLicenseTest : ::testing::Test {};

TEST_F(SquashfsLicenseTest, RegistersTheExtraAndItsCompleteDependencyClosure) {
  EXPECT_THAT(
      license::Notices(), Contains(AllOf(
                              Field("component", &license::Notice::component, "xff SquashFS extra (@xff_squashfs)"),
                              Field("spdx", &license::Notice::spdx, "Apache-2.0"))));
  EXPECT_THAT(
      license::Notices(), Contains(AllOf(
                              Field("component", &license::Notice::component, "libsqsh"),
                              Field("spdx", &license::Notice::spdx, "BSD-2-Clause"))));
  EXPECT_THAT(
      license::Notices(), Contains(AllOf(
                              Field("component", &license::Notice::component, "cextras"),
                              Field("spdx", &license::Notice::spdx, "BSD-2-Clause"))));
  EXPECT_THAT(
      license::Notices(),
      Contains(AllOf(
          Field("component", &license::Notice::component, "zlib"), Field("spdx", &license::Notice::spdx, "Zlib"))));
  EXPECT_THAT(
      license::Notices(), Contains(AllOf(
                              Field("component", &license::Notice::component, "liblzma (XZ Utils)"),
                              Field("spdx", &license::Notice::spdx, "LicenseRef-Public-Domain"))));
  EXPECT_THAT(
      license::Notices(), Contains(AllOf(
                              Field("component", &license::Notice::component, "LZ4 library"),
                              Field("spdx", &license::Notice::spdx, "BSD-2-Clause"))));
  EXPECT_THAT(
      license::Notices(), Contains(AllOf(
                              Field("component", &license::Notice::component, "Zstandard"),
                              Field("spdx", &license::Notice::spdx, "BSD-3-Clause"))));
}

}  // namespace
}  // namespace xff::squashfs
