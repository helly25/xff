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

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_backend.h"

namespace xff::archive {
namespace {

using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::Not;

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

TEST_F(ArchiveRegisterTest, ReadFormatsAndTheNameGateAgreeInBothDirections) {
  // The formats table (--help=archive) and the dive name gate must describe the same reader.
  // Forward: every declared suffix dives. Reverse: every gate suffix is owned by a declared format
  // (matched whole or by its last dotted component, which is how the gate itself matches
  // compounds) - so a suffix added to either side without the other fails here, which is the drift
  // this table exists to prevent.
  const std::vector<ReadFormatInfo> formats = ContainerReadFormats();
  ASSERT_THAT(formats, Not(IsEmpty()));
  absl::flat_hash_set<std::string> declared;
  for (const ReadFormatInfo& format : formats) {
    for (const std::string& suffix : format.suffixes) {
      EXPECT_TRUE(LooksLikeContainerName(absl::StrCat("x", suffix))) << format.name << " declares " << suffix;
      declared.insert(suffix);
      const std::string::size_type dot = suffix.rfind('.');
      if (dot != 0) {
        declared.insert(suffix.substr(dot));
      }
    }
  }
  for (const std::string_view gate : ContainerNameSuffixes()) {
    EXPECT_TRUE(declared.contains(gate)) << gate << " dives but no read format declares it";
  }
}

TEST_F(ArchiveRegisterTest, LinkingTheExtraGivesTheCoreContainerCreation) {
  EXPECT_THAT(ContainerPackingAvailable(), IsTrue());
  // The formats travel with the packer, so the CLI's pre-walk check sees the real set.
  EXPECT_THAT(ContainerPackFormats(), Contains("tar.gz"));
}

TEST_F(ArchiveRegisterTest, TheOptionVocabularyReachesTheCoreThroughTheSeam) {
  // What the CLI checks a `--pack-option` name against, and what `--help=archive` renders. Reading it
  // from the seam is the point: a name added to the writer must reach both without a second list.
  EXPECT_THAT(
      ContainerPackVocabulary(), Contains(AllOf(
                                     Field("name", &PackOptionInfo::name, "level"),
                                     Field("formats", &PackOptionInfo::formats, HasSubstr("zip")))));
}

TEST_F(ArchiveRegisterTest, TheSeamReachesTheRealWriterAndKeepsItsErrors) {
  // An output name carrying no writable format is InvalidArgument; with no registrar the seam would
  // answer Unimplemented instead, which is the regression this guards.
  EXPECT_THAT(PackContainer("/tmp/xff-register.rar", {}), StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace xff::archive
