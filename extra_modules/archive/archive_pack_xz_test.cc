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
//
// The xz half of the pack coverage, split out ONLY so it can be tagged `no_san` (see the BUILD
// comment): liblzma dispatches through a generically-typed function pointer, which UBSan's
// `function` check reports as undefined behaviour inside xz. Everything here is ordinary pack
// coverage that belongs with the rest; keeping it in its own target is what lets the rest stay
// sanitized instead of turning the check off repo-wide.
//
// Probed format by format under `-fsanitize=function`: xz is the only writer that trips it (tar,
// tar.gz, tgz, tar.bz2, tar.zst and zip are all clean and stay in archive_pack_test).

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_pack.h"
#include "xff/archive/archive_reader.h"

namespace xff::archive {
namespace {

namespace stdfs = std::filesystem;

using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::testing::Eq;
using ::testing::Field;
using ::testing::UnorderedElementsAre;

struct ArchivePackXzTest : ::testing::Test {
  void SetUp() override {
    root_ = stdfs::temp_directory_path() / "xff-pack-xz";
    std::error_code error;
    stdfs::remove_all(root_, error);
    stdfs::create_directories(root_ / "dir", error);
    Write(root_ / "one.txt", "first\n");
    Write(root_ / "dir" / "two.txt", "second\n");
  }

  void TearDown() override {
    std::error_code error;
    stdfs::remove_all(root_, error);
  }

  static void Write(const stdfs::path& path, std::string_view content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
  }

  [[nodiscard]] std::vector<PackEntry> Entries() const {
    return {
        PackEntry{.source = (root_ / "one.txt").string(), .name = "one.txt"},
        PackEntry{.source = (root_ / "dir" / "two.txt").string(), .name = "dir/two.txt"},
    };
  }

  stdfs::path root_;
};

TEST_F(ArchivePackXzTest, AnXzArchiveReadsBackWithItsNamesAndContent) {
  // That it reads back at all is what proves the format and the filter were set: a wrong filter
  // yields bytes libarchive cannot parse.
  const std::string out = (root_ / "packed.tar.xz").string();
  ASSERT_THAT(PackFiles(out, Entries()), IsOk());
  EXPECT_THAT(
      ListMembersOfFile(out),
      IsOkAndHolds(UnorderedElementsAre(Field(&Member::path, "one.txt"), Field(&Member::path, "dir/two.txt"))));
  EXPECT_THAT(ReadMemberOfFile(out, "dir/two.txt"), IsOkAndHolds(Eq("second\n")));
}

TEST_F(ArchivePackXzTest, TheXzCompressionLevelReachesTheCompressor) {
  Write(root_ / "big.txt", std::string(200'000, 'a') + "\n");
  const std::vector<PackEntry> entries = {PackEntry{.source = (root_ / "big.txt").string(), .name = "big.txt"}};
  const std::string fast = (root_ / "fast.tar.xz").string();
  const std::string small = (root_ / "small.tar.xz").string();
  ASSERT_THAT(PackFiles(fast, entries, PackSettings{.options = {{.name = "level", .value = "0"}}}), IsOk());
  ASSERT_THAT(PackFiles(small, entries, PackSettings{.options = {{.name = "level", .value = "9"}}}), IsOk());
  EXPECT_THAT(stdfs::file_size(small), ::testing::Lt(stdfs::file_size(fast)));
}

}  // namespace
}  // namespace xff::archive
