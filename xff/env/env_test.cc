// SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com)
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "xff/env/env.h"

#include <array>
#include <cstdlib>
#include <optional>
#include <string_view>

#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::env {
namespace {

using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Optional;

struct EnvTest : ::testing::Test {
  void SetUp() override { ClearForTesting(); }

  void TearDown() override { ClearForTesting(); }
};

TEST_F(EnvTest, GetReturnsInjectedValue) {
  SetForTesting("XFF_ENV_TEST_A", "hello");
  EXPECT_THAT(Get("XFF_ENV_TEST_A"), Optional(Eq("hello")));
  EXPECT_THAT(Has("XFF_ENV_TEST_A"), IsTrue());
}

TEST_F(EnvTest, GetReturnsNulloptForInjectedUnset) {
  SetForTesting("XFF_ENV_TEST_B", std::nullopt);
  EXPECT_THAT(Get("XFF_ENV_TEST_B"), Eq(std::nullopt));
  EXPECT_THAT(Has("XFF_ENV_TEST_B"), IsFalse());
}

TEST_F(EnvTest, InjectedEmptyValueCountsAsSet) {
  SetForTesting("XFF_ENV_TEST_EMPTY", "");
  EXPECT_THAT(Get("XFF_ENV_TEST_EMPTY"), Optional(Eq("")));
  EXPECT_THAT(Has("XFF_ENV_TEST_EMPTY"), IsTrue());
}

TEST_F(EnvTest, SetForTestingOverwritesPreviousValue) {
  SetForTesting("XFF_ENV_TEST_C", "first");
  SetForTesting("XFF_ENV_TEST_C", "second");
  EXPECT_THAT(Get("XFF_ENV_TEST_C"), Optional(Eq("second")));
}

TEST_F(EnvTest, ReadsRealEnvironmentAndCachesIt) {
  // setenv is not thread safe, but this test is single-threaded and mutates before any read.
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ASSERT_THAT(::setenv("XFF_ENV_TEST_REAL", "from-env", /*overwrite=*/1), Eq(0));
  EXPECT_THAT(Get("XFF_ENV_TEST_REAL"), Optional(Eq("from-env")));
  // A later real-environment change is not observed: the first read is cached.
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ASSERT_THAT(::setenv("XFF_ENV_TEST_REAL", "changed", /*overwrite=*/1), Eq(0));
  EXPECT_THAT(Get("XFF_ENV_TEST_REAL"), Optional(Eq("from-env")));
}

TEST_F(EnvTest, UnsetRealVariableReadsAsNullopt) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ASSERT_THAT(::unsetenv("XFF_ENV_TEST_ABSENT"), Eq(0));
  EXPECT_THAT(Get("XFF_ENV_TEST_ABSENT"), Eq(std::nullopt));
}

TEST_F(EnvTest, PrewarmPopulatesTheCache) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ASSERT_THAT(::setenv("XFF_ENV_TEST_WARM", "warmed", /*overwrite=*/1), Eq(0));
  constexpr std::array<std::string_view, 1> kNames = {"XFF_ENV_TEST_WARM"};
  Prewarm(absl::MakeConstSpan(kNames));
  // Change the real environment after prewarm; the cached value must win.
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ASSERT_THAT(::setenv("XFF_ENV_TEST_WARM", "later", /*overwrite=*/1), Eq(0));
  EXPECT_THAT(Get("XFF_ENV_TEST_WARM"), Optional(Eq("warmed")));
}

TEST_F(EnvTest, ClearForTestingDropsInjectedOverrides) {
  SetForTesting("XFF_ENV_TEST_D", "value");
  ClearForTesting();
  // After clearing, the injected override is gone; a fresh read falls through to getenv (unset).
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  ASSERT_THAT(::unsetenv("XFF_ENV_TEST_D"), Eq(0));
  EXPECT_THAT(Get("XFF_ENV_TEST_D"), Eq(std::nullopt));
}

}  // namespace
}  // namespace xff::env
