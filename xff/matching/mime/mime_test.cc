// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
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

#include "xff/matching/mime/mime.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

namespace xff::mime {
namespace {

using mbo::testing::IsOk;
using mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

struct MimeTest : ::testing::Test {
  void TearDown() override { EXPECT_THAT(Configure({}, ConflictPolicy::kError), IsOk()); }

  static std::string Write(std::string_view text) {
    const std::filesystem::path path =
        std::filesystem::path(::testing::TempDir()) / ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::ofstream(path) << text;
    return path.string();
  }
};

TEST_F(MimeTest, MapsKnownExtensions) {
  EXPECT_THAT(TypeForName("photo.jpg"), Eq("image/jpeg"));
  EXPECT_THAT(TypeForName("a/b/icon.png"), Eq("image/png"));
  EXPECT_THAT(TypeForName("notes.md"), Eq("text/markdown"));
  EXPECT_THAT(TypeForName("data.json"), Eq("application/json"));
}

TEST_F(MimeTest, ExtensionMatchIsCaseInsensitive) {
  EXPECT_THAT(TypeForName("PHOTO.JPG"), Eq("image/jpeg"));
  EXPECT_THAT(TypeForName("Clip.MP4"), Eq("video/mp4"));
}

TEST_F(MimeTest, UsesTheLastExtensionOfACompoundName) {
  EXPECT_THAT(TypeForName("archive.tar.gz"), Eq("application/gzip"));
}

TEST_F(MimeTest, UnknownExtensionFallsBackToOctetStream) {
  EXPECT_THAT(TypeForName("firmware.xyz"), Eq("application/octet-stream"));
}

TEST_F(MimeTest, NoExtensionOrDotfileFallsBackToOctetStream) {
  EXPECT_THAT(TypeForName("README"), Eq("application/octet-stream"));
  EXPECT_THAT(TypeForName(".bashrc"), Eq("application/octet-stream"));  // a dotfile has no extension
  EXPECT_THAT(TypeForName(""), Eq("application/octet-stream"));
}

TEST_F(MimeTest, JsonLayerOverridesMappingAndSuppliesMetadata) {
  const std::string file = Write(R"({
    "application/x-widget": {
      "description": "Widget document",
      "source": "local",
      "charset": "UTF-8",
      "compressible": true,
      "aliases": ["application/widget"],
      "extensions": [".jpg", "widget"]
    }
  })");
  EXPECT_THAT(Configure({file}, ConflictPolicy::kError), IsOk());
  const TypeInfo info = InfoForName("photo.jpg");
  EXPECT_THAT(info.type, Eq("application/x-widget"));
  EXPECT_THAT(info.Category(), Eq("application"));
  EXPECT_THAT(info.description, Eq("Widget document"));
  EXPECT_THAT(info.charset, Eq("UTF-8"));
  EXPECT_THAT(info.compressible, Eq(true));
  EXPECT_THAT(info.aliases, ElementsAre("application/widget"));
  EXPECT_THAT(TypeForName("file.widget"), Eq("application/x-widget"));
}

TEST_F(MimeTest, ConflictingClaimsAreStrictByDefaultWithExplicitFirstAndLastPolicies) {
  const std::string file = Write(R"({
    "application/x-z-first": {"extensions": ["same"]},
    "application/x-a-last": {"extensions": ["same"]}
  })");
  EXPECT_THAT(
      Configure({file}, ConflictPolicy::kError),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("extension 'same' is claimed")));
  EXPECT_THAT(Configure({file}, ConflictPolicy::kFirst), IsOk());
  EXPECT_THAT(TypeForName("x.same"), Eq("application/x-z-first"));
  EXPECT_THAT(
      Types(),
      Contains(AllOf(Field(&TypeInfo::type, "application/x-a-last"), Field(&TypeInfo::extensions, IsEmpty()))));
  EXPECT_THAT(Configure({file}, ConflictPolicy::kLast), IsOk());
  EXPECT_THAT(TypeForName("x.same"), Eq("application/x-a-last"));
  EXPECT_THAT(
      Types(),
      Contains(AllOf(Field(&TypeInfo::type, "application/x-z-first"), Field(&TypeInfo::extensions, IsEmpty()))));
}

TEST_F(MimeTest, MissingMetadataIsEmptyRatherThanInvented) {
  const TypeInfo info = InfoForName("photo.jpg");
  EXPECT_THAT(info.description, IsEmpty());
  EXPECT_THAT(info.source, IsEmpty());
  EXPECT_THAT(info.charset, IsEmpty());
  EXPECT_THAT(info.compressible, Eq(std::nullopt));
}

}  // namespace
}  // namespace xff::mime
