// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <sqsh.h>

#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::squashfs {
namespace {

using ::testing::HasSubstr;

struct LibsqshLinkTest : ::testing::Test {};

TEST_F(LibsqshLinkTest, ExposesVersionedLibrary) {
  EXPECT_THAT(std::string_view(sqsh_version()), HasSubstr("1.5.2"));
}

}  // namespace
}  // namespace xff::squashfs
