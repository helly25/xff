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

#include <optional>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::exec {
namespace {

using ::testing::Eq;
using ::testing::Optional;
using ::testing::SizeIs;

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

TEST_F(ExecTest, CaptureOutputReturnsChildStdout) {
  // Raw stdout, no trailing newline added.
  EXPECT_THAT(CaptureOutput({"/bin/sh", "-c", "printf 'hello world'"}), Optional(Eq("hello world")));
}

TEST_F(ExecTest, CaptureOutputCapturesStdoutEvenOnNonzeroExit) {
  EXPECT_THAT(CaptureOutput({"/bin/sh", "-c", "printf partial; exit 3"}), Optional(Eq("partial")));
}

TEST_F(ExecTest, CaptureOutputEmptyArgsOrSpawnFailureIsNullopt) {
  EXPECT_THAT(CaptureOutput({}), Eq(std::nullopt));
  EXPECT_THAT(CaptureOutput({"/nonexistent/xff/zzz"}), Eq(std::nullopt));  // spawn fails
}

TEST_F(ExecTest, CaptureOutputDrainsMoreThanPipeBuffer) {
  // 100 KB exceeds the pipe buffer; the child blocks on write until we read, so
  // this would deadlock if we reaped before draining.
  EXPECT_THAT(CaptureOutput({"/bin/sh", "-c", "head -c 100000 /dev/zero | tr '\\0' x"}), Optional(SizeIs(100'000U)));
}

TEST_F(ExecTest, ExecuteInDirSetsChildWorkingDirectory) {
  // The child runs with its cwd set to `dir`: chdir to "/" -> pwd -P is "/". "/" is
  // always present and accessible, including under the test sandbox.
  EXPECT_TRUE(ExecuteInDir({"/bin/sh", "-c", "test \"$(pwd -P)\" = /"}, "/", "ignored"));
}

TEST_F(ExecTest, ExecuteInDirSubstitutesBraceWithName) {
  // {} is replaced by `name` (the caller's "./<basename>"), independent of the cwd.
  EXPECT_TRUE(ExecuteInDir({"/bin/sh", "-c", "test \"{}\" = ./f.txt"}, "/", "./f.txt"));
  EXPECT_FALSE(ExecuteInDir({"/bin/sh", "-c", "test \"{}\" = ./f.txt"}, "/", "./other"));
  EXPECT_FALSE(ExecuteInDir({}, "/", "x"));  // empty command
}

TEST_F(ExecTest, ExecuteArgsInDirSpawnsVerbatimInDir) {
  EXPECT_TRUE(ExecuteArgsInDir({"/bin/sh", "-c", "test \"$(pwd -P)\" = /"}, "/"));
  EXPECT_FALSE(ExecuteArgsInDir({}, "/"));  // empty argv
  // No "{}" substitution: the literal braces reach the child unchanged.
  EXPECT_TRUE(ExecuteArgsInDir({"/bin/sh", "-c", "test '{}' = '{}'"}, "/"));
}

TEST_F(ExecTest, DirVariantsWithEmptyOrDotDirInheritCwd) {
  // An empty or "." dir means "do not chdir" -- like ExecuteArgs/Execute.
  EXPECT_TRUE(ExecuteArgsInDir({"/bin/sh", "-c", "exit 0"}, ""));
  EXPECT_TRUE(ExecuteArgsInDir({"/bin/sh", "-c", "exit 0"}, "."));
}

TEST_F(ExecTest, CaptureOutputRunsChildInDirWhenDirGiven) {
  // With dir "/", the child's cwd is "/", so `pwd -P` prints "/\n" (raw, untrimmed).
  EXPECT_THAT(CaptureOutput({"/bin/sh", "-c", "pwd -P"}, "/"), Optional(Eq("/\n")));
}

TEST_F(ExecTest, CaptureOutputDefaultDirInheritsCwd) {
  // The default (empty) dir = no chdir: the original single-argument behavior.
  EXPECT_THAT(CaptureOutput({"/bin/sh", "-c", "printf hi"}), Optional(Eq("hi")));
}

}  // namespace
}  // namespace xff::exec
