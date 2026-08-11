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

// Asserts that linking this extra makes the CORE able to open containers - by asking the seam, never
// the class. A test that called ArchiveFileSystem::Open directly would keep passing if the registrar
// were dropped from the link, which is precisely the regression worth guarding: `--archive` would go
// back to "not built into this binary" with nothing failing to build.

#include <string>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_backend.h"

namespace xff::archive {
namespace {

using ::mbo::testing::StatusIs;
using ::testing::IsTrue;

struct ArchiveRegisterTest : ::testing::Test {};

TEST_F(ArchiveRegisterTest, LinkingTheExtraGivesTheCoreArchiveSupport) {
  EXPECT_THAT(ContainerSupportAvailable(), IsTrue());
}

TEST_F(ArchiveRegisterTest, TheSeamReachesTheRealReaderAndKeepsItsErrors) {
  // A path that exists but is not an archive must come back InvalidArgument (the walk then treats it
  // as an ordinary file). Reaching the real reader is the point: with no registrar the seam would
  // answer Unimplemented instead.
  EXPECT_THAT(OpenContainer("/etc/hosts"), StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace xff::archive
