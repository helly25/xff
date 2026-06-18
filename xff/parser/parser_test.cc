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

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "absl/status/status.h"
#include "mbo/testing/status.h"
#include "xff/parser/ast.h"
#include "xff/registry/descriptor.h"

namespace xff::parser {
namespace {

using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;

struct ParserTest : ::testing::Test {};

TEST_F(ParserTest, GlobalsRootsExpression) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({"--color", ".", "-type", "f"}));
  EXPECT_THAT(cmd.globals, ElementsAre("--color"));
  EXPECT_THAT(cmd.roots, ElementsAre("."));
  ASSERT_THAT(cmd.expression, NotNull());
  EXPECT_THAT(cmd.expression->kind, Eq(Expr::Kind::kPredicate));
  ASSERT_THAT(cmd.expression->descriptor, NotNull());
  EXPECT_THAT(cmd.expression->descriptor->name, Eq("-type"));
  EXPECT_THAT(cmd.expression->args, ElementsAre("f"));
}

TEST_F(ParserTest, NoExpressionIsNull) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({"."}));
  EXPECT_THAT(cmd.roots, ElementsAre("."));
  EXPECT_THAT(cmd.expression, IsNull());
}

TEST_F(ParserTest, OrIsLowerThanImplicitAnd) {
  // `-type f -name x -o -name y` => Or( And(-type f, -name x), -name y )
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-type", "f", "-name", "x", "-o", "-name", "y"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Eq(Expr::Kind::kOr));
  EXPECT_THAT(root.lhs->kind, Eq(Expr::Kind::kAnd));
  ASSERT_THAT(root.rhs->kind, Eq(Expr::Kind::kPredicate));
  EXPECT_THAT(root.rhs->descriptor->name, Eq("-name"));
}

TEST_F(ParserTest, NotBindsTightest) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "!", "-type", "d"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Eq(Expr::Kind::kNot));
  ASSERT_THAT(root.lhs->kind, Eq(Expr::Kind::kPredicate));
  EXPECT_THAT(root.lhs->descriptor->name, Eq("-type"));
}

TEST_F(ParserTest, ParensGroup) {
  // `( -type f -o -type d ) -print` => And( Or(...), -print )
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "(", "-type", "f", "-o", "-type", "d", ")", "-print"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Eq(Expr::Kind::kAnd));
  EXPECT_THAT(root.lhs->kind, Eq(Expr::Kind::kOr));
  ASSERT_THAT(root.rhs->kind, Eq(Expr::Kind::kPredicate));
  EXPECT_THAT(root.rhs->descriptor->name, Eq("-print"));
}

TEST_F(ParserTest, CommaIsLowestPrecedence) {
  // `-type f -o -type d , -name x` => Comma( Or(-type f, -type d), -name x )
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-type", "f", "-o", "-type", "d", ",", "-name", "x"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Eq(Expr::Kind::kComma));
  EXPECT_THAT(root.lhs->kind, Eq(Expr::Kind::kOr));
  ASSERT_THAT(root.rhs->kind, Eq(Expr::Kind::kPredicate));
  EXPECT_THAT(root.rhs->descriptor->name, Eq("-name"));
}

TEST_F(ParserTest, ExecCollectsCommandUntilSemicolon) {
  // `-exec echo {} ; -print` => And( -exec[echo, {}], -print ); the ';' is consumed.
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-exec", "echo", "{}", ";", "-print"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Eq(Expr::Kind::kAnd));
  ASSERT_THAT(root.lhs->kind, Eq(Expr::Kind::kPredicate));
  EXPECT_THAT(root.lhs->descriptor->name, Eq("-exec"));
  EXPECT_THAT(root.lhs->args, ElementsAre("echo", "{}"));
  EXPECT_THAT(root.rhs->descriptor->name, Eq("-print"));
}

TEST_F(ParserTest, ExecWithoutTerminatorErrors) {
  EXPECT_THAT(Parse({".", "-exec", "echo", "{}"}), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ParserTest, Errors) {
  using ::absl::StatusCode;
  EXPECT_THAT(Parse({".", "-bogus"}), StatusIs(StatusCode::kInvalidArgument));            // unknown predicate
  EXPECT_THAT(Parse({".", "-name"}), StatusIs(StatusCode::kInvalidArgument));             // missing argument
  EXPECT_THAT(Parse({".", "(", "-type", "f"}), StatusIs(StatusCode::kInvalidArgument));   // unbalanced '('
  EXPECT_THAT(Parse({".", "-o", "-type", "f"}), StatusIs(StatusCode::kInvalidArgument));  // leading operator
}

}  // namespace
}  // namespace xff::parser
