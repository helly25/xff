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

#include "xff/registry/registry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "xff/registry/descriptor.h"

namespace xff::registry {
namespace {

using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;

struct RegistryTest : ::testing::Test {};

TEST_F(RegistryTest, LooksUpKnownTokens) {
  const Descriptor* name = Lookup("-name");
  ASSERT_THAT(name, NotNull());
  EXPECT_THAT(name->kind, Eq(Kind::kTest));
  EXPECT_THAT(name->arity, Eq(1));

  const Descriptor* print = Lookup("-print");
  ASSERT_THAT(print, NotNull());
  EXPECT_THAT(print->kind, Eq(Kind::kAction));
  EXPECT_THAT(print->arity, Eq(0));

  const Descriptor* or_op = Lookup("-o");
  ASSERT_THAT(or_op, NotNull());
  EXPECT_THAT(or_op->kind, Eq(Kind::kOperator));

  const Descriptor* bang = Lookup("!");
  ASSERT_THAT(bang, NotNull());
  EXPECT_THAT(bang->kind, Eq(Kind::kOperator));
}

TEST_F(RegistryTest, UnknownTokenIsNull) {
  EXPECT_THAT(Lookup("-nonexistent"), IsNull());
  EXPECT_THAT(Lookup(""), IsNull());
  EXPECT_THAT(Lookup("."), IsNull());
}

}  // namespace
}  // namespace xff::registry
