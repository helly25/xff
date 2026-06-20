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

TEST_F(FieldsTest, HumanSizeAndSuffixes) {
  EXPECT_THAT(Render("{size:h}", "f", Meta(vfs::FileType::kRegular, 1536), 0), Eq("1.5K"));
  EXPECT_THAT(Render("{size:h}", "f", Meta(vfs::FileType::kRegular, 500), 0), Eq("500"));  // < 1 KiB: plain
  EXPECT_THAT(Render("{size}", "f", Meta(vfs::FileType::kRegular, 1536), 0), Eq("1536"));  // no qualifier
  EXPECT_THAT(Render("{suffixes}", "a/b/file.tar.gz", Meta(vfs::FileType::kRegular, 0), 0), Eq(".tar.gz"));
  EXPECT_THAT(Render("{suffixes}", "a/b/file", Meta(vfs::FileType::kRegular, 0), 0), Eq(""));
}

TEST_F(FieldsTest, QuotedQualifierCarriesBracesColonsAndQuotes) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  md.mtime = absl::FromUnixSeconds(1700000000);  // 2023-11-14, mid-month: the year is timezone-stable
  // Escaped quotes/braces survive into the strftime format; the inner '}' does not end the field.
  EXPECT_THAT(Render(R"({mtime:"{\"y\":\"%Y\"}"})", "f", md, 0), Eq(R"({"y":"2023"})"));
  // A quoted qualifier is dequoted before matching, so {size:"h"} still selects human-readable size.
  EXPECT_THAT(Render(R"({size:"h"})", "f", Meta(vfs::FileType::kRegular, 1536), 0), Eq("1.5K"));
  // An unterminated quoted qualifier leaves the '{' and the remaining text literal.
  EXPECT_THAT(Render(R"({mtime:"%Y)", "f", md, 0), Eq(R"({mtime:"%Y)"));
}

TEST_F(FieldsTest, CompiledTemplateRendersManyEntries) {
  const Template compiled = Template::Compile("{name}={size:h}");  // parsed once, reused below
  const vfs::Metadata small = Meta(vfs::FileType::kRegular, 1);
  const vfs::Metadata big = Meta(vfs::FileType::kRegular, 1536);
  EXPECT_THAT(compiled.Render(RenderContext{.path = "a/x", .metadata = small, .depth = 0}), Eq("x=1"));
  EXPECT_THAT(compiled.Render(RenderContext{.path = "b/big", .metadata = big, .depth = 0}), Eq("big=1.5K"));
}

TEST_F(FieldsTest, FieldNameAliasesResolveToTheSameRenderer) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  md.mode = 0700;
  EXPECT_THAT(Render("{file}|{name}", "a/b.txt", md, 0), Eq("b.txt|b.txt"));
  EXPECT_THAT(Render("{extension}|{ext}", "a/b.txt", md, 0), Eq("txt|txt"));
  EXPECT_THAT(Render("{perm}|{mode}", "a/b.txt", md, 0), Eq("700|700"));
}

TEST_F(FieldsTest, RootFieldReportsTheSearchRoot) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const Template compiled = Template::Compile("{root}|{path}");
  EXPECT_THAT(
      compiled.Render(RenderContext{.path = "r/sub/f", .root = "r", .metadata = md, .depth = 2}), Eq("r|r/sub/f"));
  // An empty root (e.g. via the rootless convenience overload) renders empty.
  EXPECT_THAT(compiled.Render(RenderContext{.path = "x", .metadata = md, .depth = 0}), Eq("|x"));
}

}  // namespace
}  // namespace xff::fields
