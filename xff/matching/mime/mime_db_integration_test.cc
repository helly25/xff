// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/matching/mime/mime.h"

namespace xff::mime {
namespace {

using ::mbo::testing::IsOk;
using ::testing::Eq;

struct MimeDbIntegrationTest : ::testing::Test {};

TEST_F(MimeDbIntegrationTest, LinkedDatabaseExtendsCoreAndCarriesMetadata) {
  EXPECT_THAT(Configure({}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(TypeForName("document.azw"), Eq("application/vnd.amazon.ebook"));
  const TypeInfo info = InfoForName("font.woff2");
  EXPECT_THAT(info.type, Eq("font/woff2"));
  EXPECT_THAT(info.Category(), Eq("font"));
  EXPECT_THAT(info.source, Eq("iana"));
}

}  // namespace
}  // namespace xff::mime
