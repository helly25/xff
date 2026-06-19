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

#include "xff/fields/fields.h"

#include <cstdint>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "absl/time/time.h"
#include "xff/vfs/entry.h"

namespace xff::fields {
namespace {

using ::testing::Eq;
using ::testing::HasSubstr;

struct FieldsTest : ::testing::Test {
  static vfs::Metadata Meta(vfs::FileType type, std::uint64_t size) {
    vfs::Metadata md;
    md.type = type;
    md.size = size;
    return md;
  }
};

TEST_F(FieldsTest, SubstitutesPathComponentsAndMetadata) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 12);
  EXPECT_THAT(
      Render("{name} in {dir} [{ext}] {size}b {type}", "a/b/file.tar.gz", md, 2),
      Eq("file.tar.gz in a/b [gz] 12b f"));
  EXPECT_THAT(Render("{stem}", "a/b/file.tar.gz", md, 2), Eq("file.tar"));
}

TEST_F(FieldsTest, DirOfTopLevelEntryIsDot) {
  EXPECT_THAT(Render("{dir}", "file", Meta(vfs::FileType::kRegular, 0), 0), Eq("."));
}

TEST_F(FieldsTest, DoubledBracesAreLiteral) {
  EXPECT_THAT(Render("{{x}}={name}", "p/q", Meta(vfs::FileType::kRegular, 0), 0), Eq("{x}=q"));
}

TEST_F(FieldsTest, UnknownFieldRendersEmpty) {
  EXPECT_THAT(Render("[{bogus}]", "p", Meta(vfs::FileType::kRegular, 0), 0), Eq("[]"));
}

TEST_F(FieldsTest, TimeFieldQualifiers) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  md.mtime = absl::FromUnixSeconds(1700000000);  // 2023-11-14, mid-month: the year is timezone-stable
  EXPECT_THAT(Render("{mtime:epoch}", "f", md, 0), Eq("1700000000"));
  EXPECT_THAT(Render("{mtime:%Y}", "f", md, 0), Eq("2023"));
  EXPECT_THAT(Render("{mtime:iso}", "f", md, 0), HasSubstr("2023"));
  EXPECT_THAT(Render("{mtime}", "f", md, 0), HasSubstr("2023"));  // default ISO-8601
}

TEST_F(FieldsTest, ModeAndOwnerFields) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  md.mode = 0644;
  md.uid = 1234567;  // unlikely to have a passwd/group entry -> numeric fallback (find's %u/%g)
  md.gid = 1234567;
  EXPECT_THAT(Render("{mode} {user}:{group}", "f", md, 0), Eq("644 1234567:1234567"));
}

}  // namespace
}  // namespace xff::fields
