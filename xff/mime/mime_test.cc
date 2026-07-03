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

#include "xff/mime/mime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::mime {
namespace {

using ::testing::Eq;

struct MimeTest : ::testing::Test {};

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

}  // namespace
}  // namespace xff::mime
