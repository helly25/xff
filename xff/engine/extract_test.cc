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

#include "xff/engine/extract.h"

#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

namespace stdfs = std::filesystem;

using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::EndsWith;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Ne;
using ::testing::SizeIs;

// A filesystem that answers content by path and nothing else, standing in for a mounted container:
// what the extractor needs is exactly ReadContent, and a member is not a path the real filesystem
// has.
class MemberFs : public vfs::FileSystem {
 public:
  void Add(std::string path, std::string content) { content_.emplace_back(std::move(path), std::move(content)); }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    for (const auto& [member, content] : content_) {
      if (member == path) {
        return content;
      }
    }
    return absl::NotFoundError(absl::StrCat("no such member: ", path));
  }

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view) const override {
    return absl::UnimplementedError("unused");
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view, bool) const override {
    return absl::UnimplementedError("unused");
  }

  absl::Status Remove(std::string_view) const override { return absl::UnimplementedError("unused"); }

  bool Access(std::string_view, vfs::AccessMode) const override { return false; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return absl::UnimplementedError("unused"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return absl::UnimplementedError("unused"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

 private:
  std::vector<std::pair<std::string, std::string>> content_;
};

struct ExtractTest : ::testing::Test {
  static std::string Read(const std::string& path) {
    const std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
  }

  MemberFs fs_;
};

TEST_F(ExtractTest, AnExtractedMemberIsARealFileWithTheMembersNameAndBytes) {
  // The whole point: after this, a child process opens an ordinary path and needs to know nothing
  // about archives. The NAME carries over too, so a tool keying on the extension still works.
  fs_.Add("a.tar!dir/two.txt", "two\n");
  ExtractedMembers extracted;
  const absl::StatusOr<std::string> path = extracted.Extract(fs_, "a.tar!dir/two.txt");
  ASSERT_THAT(path, IsOk());
  EXPECT_THAT(*path, EndsWith("/two.txt"));
  EXPECT_THAT(Read(*path), "two\n");
  EXPECT_THAT(extracted.Held(), SizeIs(1));
}

TEST_F(ExtractTest, TwoMembersWithTheSameNameGetTheirOwnDirectories) {
  // A member path is unique but a member NAME is not, and the name is what the temporary file must
  // keep - so the uniqueness has to live in the directory instead.
  fs_.Add("a.tar!README", "from a\n");
  fs_.Add("b.tar!README", "from b\n");
  ExtractedMembers extracted;
  const absl::StatusOr<std::string> first = extracted.Extract(fs_, "a.tar!README");
  const absl::StatusOr<std::string> second = extracted.Extract(fs_, "b.tar!README");
  ASSERT_THAT(first, IsOk());
  ASSERT_THAT(second, IsOk());
  EXPECT_THAT(*first, Ne(*second));
  EXPECT_THAT(Read(*first), "from a\n");
  EXPECT_THAT(Read(*second), "from b\n");
}

TEST_F(ExtractTest, ReleaseRemovesTheFileAndItsDirectory) {
  fs_.Add("a.tar!one.txt", "one\n");
  ExtractedMembers extracted;
  const absl::StatusOr<std::string> path = extracted.Extract(fs_, "a.tar!one.txt");
  ASSERT_THAT(path, IsOk());
  const stdfs::path dir = stdfs::path(*path).parent_path();
  extracted.Release(*path);
  EXPECT_THAT(stdfs::exists(*path), IsFalse());
  EXPECT_THAT(stdfs::exists(dir), IsFalse());
  EXPECT_THAT(extracted.Held(), IsEmpty());
  extracted.Release(*path);  // releasing twice is not an error: the caller may release unconditionally
  EXPECT_THAT(extracted.Held(), IsEmpty());
}

TEST_F(ExtractTest, WhatIsStillHeldIsRemovedWhenTheRunEnds) {
  // A `-exec ... +` batch runs after the walk, so its members stay extracted until then; nothing may
  // survive the run itself.
  fs_.Add("a.tar!one.txt", "one\n");
  std::string path;
  {
    ExtractedMembers extracted;
    const absl::StatusOr<std::string> extracted_path = extracted.Extract(fs_, "a.tar!one.txt");
    ASSERT_THAT(extracted_path, IsOk());
    path = *extracted_path;
    EXPECT_THAT(stdfs::exists(path), IsTrue());
  }
  EXPECT_THAT(stdfs::exists(path), IsFalse());
  EXPECT_THAT(stdfs::exists(stdfs::path(path).parent_path()), IsFalse());
}

TEST_F(ExtractTest, AMemberThatCannotBeReadIsAnErrorNotAnEmptyFile) {
  // Extraction is the step that can fail (a corrupt container, a truncated member); handing the child
  // an empty file would look like success and be wrong.
  ExtractedMembers extracted;
  EXPECT_THAT(extracted.Extract(fs_, "a.tar!missing"), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(extracted.Held(), IsEmpty());
}

}  // namespace
}  // namespace xff::engine
