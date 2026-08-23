// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/matching/language/language.h"

namespace xff::language {
namespace {

using ::mbo::testing::IsOk;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Optional;

struct LanguageDbIntegrationTest : ::testing::Test {};

TEST_F(LanguageDbIntegrationTest, LinkedCompressedDatabaseExtendsCoreAndCarriesMetadata) {
  EXPECT_THAT(Configure({}, ConflictPolicy::kError), IsOk());
  EXPECT_THAT(LanguageForName("module.bsl"), Eq("1C Enterprise"));
  EXPECT_THAT(LanguageForName("types.d.ts"), Eq("TypeScript"));
  EXPECT_THAT(
      InfoForName("main.cpp"),
      Optional(AllOf(
          Field(&LanguageInfo::name, "C++"), Field(&LanguageInfo::type, "programming"),
          Field(&LanguageInfo::color, "#f34b7d"), Field(&LanguageInfo::source, "github-linguist 9.6.0"),
          Field(&LanguageInfo::aliases, Contains("cpp")))));
}

}  // namespace
}  // namespace xff::language
