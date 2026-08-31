// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/asar/asar_register.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/archive/archive_backend.h"
#include "xff/license/notice.h"

namespace xff::asar {
namespace {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Field;
using ::testing::IsTrue;

struct AsarRegisterTest : ::testing::Test {};

TEST_F(AsarRegisterTest, RegistersTheFormatGateAndItsSeparateExtensionNotice) {
  RegisterAsarBackend();
  EXPECT_THAT(
      archive::ContainerReadFormats(), Contains(AllOf(
                                           Field("name", &archive::ReadFormatInfo::name, "asar"),
                                           Field("suffixes", &archive::ReadFormatInfo::suffixes, Contains(".asar")))));
  EXPECT_THAT(archive::LooksLikeContainerName("application.ASAR"), IsTrue());
  EXPECT_THAT(
      license::Notices(), Contains(AllOf(
                              Field("section", &license::Notice::section, "Electron ASAR (@xff_asar)"),
                              Field("component", &license::Notice::component, "xff Electron ASAR extra (@xff_asar)"),
                              Field("spdx", &license::Notice::spdx, "Apache-2.0"))));
}

}  // namespace
}  // namespace xff::asar
