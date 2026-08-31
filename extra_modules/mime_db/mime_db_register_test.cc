// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/license/notice.h"
#include "xff/matching/mime/database.h"

namespace xff::mime_db {
namespace {

using ::testing::Contains;
using ::testing::Field;

struct MimeDbRegisterTest : ::testing::Test {};

TEST_F(MimeDbRegisterTest, RegistersDataAndNotices) {
  EXPECT_THAT(mime::Databases(), Contains(Field(&mime::Database::name, "mime-db 1.54.0")));
  EXPECT_THAT(license::Notices(), Contains(Field(&license::Notice::component, "mime-db 1.54.0")));
}

}  // namespace
}  // namespace xff::mime_db
