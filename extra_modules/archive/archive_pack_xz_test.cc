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
//
// The liblzma half of the pack coverage, split out ONLY so it can be tagged `no_san` (see the BUILD
// comment): liblzma dispatches through a generically-typed function pointer, which UBSan's
// `function` check reports as undefined behaviour inside xz. Everything here is ordinary pack
// coverage that belongs with the rest; keeping it in its own target is what lets the rest stay
// sanitized instead of turning the check off repo-wide.
//
// Probed format by format under `-fsanitize=function`: the other writers stay in archive_pack_test.

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
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
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

// The whole preset range liblzma documents, so a build that narrowed it would fail here rather than
// silently accept a level it ignores.
constexpr std::array kXzLevels = std::to_array<std::string_view>({
    "0",
    "1",
    "6",
    "9",
});

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

TEST_F(ArchivePackXzTest, LiblzmaArchivesReadBackWithTheirNamesAndContent) {
  // That it reads back at all is what proves the format and the filter were set: a wrong filter
  // yields bytes libarchive cannot parse.
  static constexpr std::array kNames = std::to_array<std::string_view>({
      "packed.tar.xz",
      "packed.txz",
      "packed.tar.lzma",
      "packed.tlz",
      "packed.tar.lz",
  });
  for (const std::string_view name : kNames) {
    const std::string out = (root_ / name).string();
    ASSERT_THAT(PackFiles(out, Entries(), PackSettings{.options = {{.name = "level", .value = "3"}}}), IsOk()) << name;
    EXPECT_THAT(
        ListMembersOfFile(out),
        IsOkAndHolds(UnorderedElementsAre(Field(&Member::path, "one.txt"), Field(&Member::path, "dir/two.txt"))))
        << name;
    EXPECT_THAT(ReadMemberOfFile(out, "dir/two.txt"), IsOkAndHolds(Eq("second\n"))) << name;
  }
}

TEST_F(ArchivePackXzTest, XzShortcutAcceptsTheThreadsOption) {
  const std::string out = (root_ / "threaded.txz").string();
  EXPECT_THAT(PackFiles(out, Entries(), PackSettings{.options = {{.name = "threads", .value = "2"}}}), IsOk());
}

TEST_F(ArchivePackXzTest, EveryXzLevelIsAcceptedAndStillReadsBack) {
  // Deliberately NOT a size comparison. liblzma's presets differ mostly in dictionary size, so on
  // trivially compressible input preset 0 already reaches the same output as preset 9 - and whether
  // it does depends on the liblzma build (it did on macOS CI and did not locally). That the level
  // reaches the compressor is proven where it IS guaranteed, on gzip, in archive_pack_test; what
  // belongs here is that xz accepts the whole range and produces an archive the reader can open.
  Write(root_ / "big.txt", std::string(200'000, 'a') + "\n");
  const std::vector<PackEntry> entries = {PackEntry{.source = (root_ / "big.txt").string(), .name = "big.txt"}};
  for (const std::string_view level : kXzLevels) {
    const std::string out = (root_ / absl::StrCat("level-", level, ".tar.xz")).string();
    ASSERT_THAT(
        PackFiles(out, entries, PackSettings{.options = {{.name = "level", .value = std::string(level)}}}), IsOk())
        << level;
    EXPECT_THAT(ReadMemberOfFile(out, "big.txt"), IsOkAndHolds(SizeIs(200'001))) << level;
  }
}

}  // namespace
}  // namespace xff::archive
