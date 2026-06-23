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

using ::testing::Field;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Pointee;

struct RegistryTest : ::testing::Test {};

TEST_F(RegistryTest, LooksUpKnownTokens) {
  const Descriptor* name = Lookup("-name");
  ASSERT_THAT(name, NotNull());
  EXPECT_THAT(name->kind, Kind::kTest);
  EXPECT_THAT(name->arity, 1);

  const Descriptor* print = Lookup("-print");
  ASSERT_THAT(print, NotNull());
  EXPECT_THAT(print->kind, Kind::kAction);
  EXPECT_THAT(print->arity, 0);

  const Descriptor* or_op = Lookup("-o");
  ASSERT_THAT(or_op, NotNull());
  EXPECT_THAT(or_op->kind, Kind::kOperator);

  const Descriptor* bang = Lookup("!");
  ASSERT_THAT(bang, NotNull());
  EXPECT_THAT(bang->kind, Kind::kOperator);
}

TEST_F(RegistryTest, UnknownTokenIsNull) {
  EXPECT_THAT(Lookup("-nonexistent"), IsNull());
  EXPECT_THAT(Lookup(""), IsNull());
  EXPECT_THAT(Lookup("."), IsNull());
}

TEST_F(RegistryTest, SecurityRelevantPrimariesAreClassified) {
  // The exec family runs arbitrary commands (kSecurity); -delete loses data
  // (kSafety); everything else is unclassified (kNone). The config policy gate
  // (phase C) keys its safe-by-default deny off this.
  for (const char* const name : {"-exec", "-execdir", "-ok", "-okdir", "-capture", "-capturedir"}) {
    EXPECT_THAT(Lookup(name), Pointee(Field("safety", &Descriptor::safety, Safety::kSecurity))) << name;
  }
  EXPECT_THAT(Lookup("-delete"), Pointee(Field("safety", &Descriptor::safety, Safety::kSafety)));
  EXPECT_THAT(Lookup("-name"), Pointee(Field("safety", &Descriptor::safety, Safety::kNone)));
  EXPECT_THAT(Lookup("-print"), Pointee(Field("safety", &Descriptor::safety, Safety::kNone)));
}

TEST_F(RegistryTest, CaptureFamilyDeclaresLabelRegexBinding) {
  // -capture/-capturedir carry an attached =NAME[=REGEX] on the token; the parser
  // reads this binding from the registry instead of hardcoding the names.
  EXPECT_THAT(Lookup("-capture"), Pointee(Field("binding", &Descriptor::binding, Binding::kLabelRegex)));
  EXPECT_THAT(Lookup("-capturedir"), Pointee(Field("binding", &Descriptor::binding, Binding::kLabelRegex)));
  EXPECT_THAT(Lookup("-exec"), Pointee(Field("binding", &Descriptor::binding, Binding::kNone)));  // no =label
  EXPECT_THAT(Lookup("-name"), Pointee(Field("binding", &Descriptor::binding, Binding::kNone)));
}

TEST_F(RegistryTest, XffExtensionsAreStyleTagged) {
  // The xff-native primaries carry Style::kXff so the strict find style (phase D)
  // can reject them; everything inherited from find stays kFind (the default).
  for (const char* const name : {"-println", "-printfln", "-capture", "-capturedir"}) {
    EXPECT_THAT(Lookup(name), Pointee(Field("style", &Descriptor::style, Style::kXff))) << name;
  }
  for (const char* const name : {"-print", "-printf", "-name", "-exec", "-delete"}) {
    EXPECT_THAT(Lookup(name), Pointee(Field("style", &Descriptor::style, Style::kFind))) << name;
  }
}

}  // namespace
}  // namespace xff::registry
