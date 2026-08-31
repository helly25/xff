// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/license/notice.h"

namespace xff::brotli {
namespace {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Not;

struct BrotliLicenseTest : ::testing::Test {};

TEST_F(BrotliLicenseTest, RegistersSeparateExtensionAndLibraryNoticesWithTheMitBody) {
  EXPECT_THAT(
      license::Notices(), Contains(AllOf(
                              Field("component", &license::Notice::component, "xff Brotli extra (@xff_brotli)"),
                              Field("spdx", &license::Notice::spdx, "Apache-2.0"))));
  EXPECT_THAT(
      license::Notices(),
      Contains(AllOf(
          Field("component", &license::Notice::component, "Brotli"), Field("spdx", &license::Notice::spdx, "MIT"))));
  EXPECT_THAT(license::LicenseBodyFor("MIT"), Not(IsEmpty()));
}

}  // namespace
}  // namespace xff::brotli
