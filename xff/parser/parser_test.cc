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

#include "xff/parser/parser.h"

#include "gtest/gtest.h"

namespace xff::parser {
namespace {

TEST(ParserTest, SingleRoot) {
  const Command cmd = Parse({"."});
  EXPECT_TRUE(cmd.globals.empty());
  ASSERT_EQ(cmd.roots.size(), 1U);
  EXPECT_EQ(cmd.roots[0], ".");
  EXPECT_TRUE(cmd.expression.empty());
}

TEST(ParserTest, GlobalsRootsExpression) {
  const Command cmd = Parse({"--color", ".", "-type", "f"});
  ASSERT_EQ(cmd.globals.size(), 1U);
  EXPECT_EQ(cmd.globals[0], "--color");
  ASSERT_EQ(cmd.roots.size(), 1U);
  EXPECT_EQ(cmd.roots[0], ".");
  ASSERT_EQ(cmd.expression.size(), 2U);
  EXPECT_EQ(cmd.expression[0], "-type");
  EXPECT_EQ(cmd.expression[1], "f");
}

TEST(ParserTest, DoubleDashEndsGlobals) {
  const Command cmd = Parse({"--color", "--", "src", "-name", "x"});
  ASSERT_EQ(cmd.globals.size(), 1U);
  EXPECT_EQ(cmd.globals[0], "--color");
  ASSERT_EQ(cmd.roots.size(), 1U);
  EXPECT_EQ(cmd.roots[0], "src");
  ASSERT_EQ(cmd.expression.size(), 2U);
  EXPECT_EQ(cmd.expression[0], "-name");
}

TEST(ParserTest, PlusTokenIsGlobalNotRoot) {
  const Command cmd = Parse({"-g+", "."});
  ASSERT_EQ(cmd.globals.size(), 1U);
  EXPECT_EQ(cmd.globals[0], "-g+");
  ASSERT_EQ(cmd.roots.size(), 1U);
  EXPECT_EQ(cmd.roots[0], ".");
}

}  // namespace
}  // namespace xff::parser
