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

#include "xff/exec/exec.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::exec {
namespace {

// /bin/sh is used as a portable, always-present command so these tests do not
// depend on PATH lookup or any particular utility being installed.
struct ExecTest : ::testing::Test {};

TEST_F(ExecTest, ReturnsTrueWhenChildExitsZero) {
  EXPECT_TRUE(Execute({"/bin/sh", "-c", "exit 0"}, "ignored"));
}

TEST_F(ExecTest, ReturnsFalseWhenChildExitsNonzero) {
  EXPECT_FALSE(Execute({"/bin/sh", "-c", "exit 7"}, "ignored"));
}

TEST_F(ExecTest, SubstitutesBracePlaceholderWithPath) {
  // {} is replaced by the path everywhere it appears in a token.
  EXPECT_TRUE(Execute({"/bin/sh", "-c", "test \"{}\" = /a/b"}, "/a/b"));
  EXPECT_FALSE(Execute({"/bin/sh", "-c", "test \"{}\" = /a/b"}, "/zz"));
}

TEST_F(ExecTest, EmptyCommandIsFalse) {
  EXPECT_FALSE(Execute({}, "x"));
}

TEST_F(ExecTest, ExecuteArgsSpawnsVerbatimWithoutSubstitution) {
  EXPECT_TRUE(ExecuteArgs({"/bin/sh", "-c", "exit 0"}));
  EXPECT_FALSE(ExecuteArgs({"/bin/sh", "-c", "exit 7"}));
  EXPECT_FALSE(ExecuteArgs({}));  // empty argv
  // No "{}" substitution: the literal braces reach the child unchanged, so both
  // sides of the comparison are the literal string "{}".
  EXPECT_TRUE(ExecuteArgs({"/bin/sh", "-c", "test '{}' = '{}'"}));
}

}  // namespace
}  // namespace xff::exec
