// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/matching/mime/database.h"

namespace xff::mime {
namespace {

using ::testing::Contains;
using ::testing::Field;

struct MimeDatabaseTest : ::testing::Test {};

TEST_F(MimeDatabaseTest, RegistrarContributesLinkedDatabase) {
  const DatabaseRegistrar registrar{{.name = "test", .json = R"({})"}};
  EXPECT_THAT(Databases(), Contains(Field(&Database::name, "test")));
}

}  // namespace
}  // namespace xff::mime
