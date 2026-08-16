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

#include "xff/fuse/fuse_backend.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::fuse {
namespace {

using ::testing::IsFalse;
using ::testing::IsTrue;

struct FuseBackendTest : ::testing::Test {};

// Both states in their forced order: this binary links no registration TU, so the slot answers
// false (the lean binary's answer) until the registrar - what @xff_fuse's registration TU declares
// at file scope - flips it.
TEST_F(FuseBackendTest, UnregisteredIsTheLeanAnswerAndRegistrationFlipsIt) {
  ASSERT_THAT(MountSupportAvailable(), IsFalse());
  const MountSupportRegistrar registrar{};
  EXPECT_THAT(MountSupportAvailable(), IsTrue());
}

}  // namespace
}  // namespace xff::fuse
