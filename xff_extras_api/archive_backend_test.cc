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

#include "xff/archive/archive_backend.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/archive/member_path.h"
#include "xff/vfs/filesystem.h"

namespace xff::archive {
namespace {

using ::mbo::testing::StatusIs;
using ::testing::HasSubstr;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::NotNull;

// A filesystem that answers nothing: this test is about the SLOT, not about any backend. Only
// FsType is given a value, so a test can tell the stub apart from a real one.
class StubFileSystem : public vfs::FileSystem {
 public:
  [[nodiscard]] absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view /*dir*/) const override {
    return std::vector<vfs::Entry>{};
  }

  [[nodiscard]] absl::StatusOr<vfs::Metadata> Stat(std::string_view /*path*/, bool /*follow*/) const override {
    return absl::UnimplementedError("stub");
  }

  [[nodiscard]] absl::Status Remove(std::string_view /*path*/) const override {
    return absl::UnimplementedError("stub");
  }

  [[nodiscard]] bool Access(std::string_view /*path*/, vfs::AccessMode /*mode*/) const override { return false; }

  [[nodiscard]] absl::StatusOr<std::string> ReadLink(std::string_view /*path*/) const override {
    return absl::UnimplementedError("stub");
  }

  [[nodiscard]] absl::StatusOr<std::string> FsType(std::string_view /*path*/) const override { return "stub"; }

  [[nodiscard]] absl::StatusOr<bool> IsCaseSensitive(std::string_view /*path*/) const override { return true; }

  [[nodiscard]] absl::StatusOr<std::string> ReadContent(std::string_view /*path*/) const override {
    return absl::UnimplementedError("stub");
  }
};

// The registration slot is process-wide, so a test that installs an opener has to put the slot back
// the way it found it - otherwise the case order decides what the next case sees.
struct ArchiveBackendTest : ::testing::Test {
  void TearDown() override { RegisterContainerOpener(ContainerOpener()); }
};

TEST_F(ArchiveBackendTest, WithNoBackendThereIsNoSupportAndOpeningSaysWhy) {
  // The lean build: the `--archive` surface exists and is documented, but nothing can look inside a
  // container. Unimplemented rather than InvalidArgument, because nothing is wrong with the path.
  EXPECT_THAT(ContainerSupportAvailable(), ::testing::IsFalse());
  EXPECT_THAT(
      OpenContainer("some.tar"), StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("without archive support")));
}

TEST_F(ArchiveBackendTest, TheNameGateAcceptsFormatsAndPackagesAlike) {
  // The gate in front of the reader under `--archive=all`: a name is enough to be OFFERED, never
  // enough to be an archive. Both a bare format suffix and a package that is one underneath count,
  // and the comparison folds case because filesystems shout.
  constexpr std::array kOffered = std::to_array<std::string_view>({
      "a.tar",
      "a.tar.gz",
      "a.tgz",
      "a.zip",
      "A.ZIP",
      "x.jar",
      "x.whl",
      "x.phar",
      "x.rpm",
      "x.7z",
  });
  for (const std::string_view name : kOffered) {
    EXPECT_THAT(LooksLikeContainerName(name), IsTrue()) << name;
  }
}

TEST_F(ArchiveBackendTest, TheNameGateRejectsEverydayFiles) {
  // What the gate is FOR: walking a source tree must not open and format-bid every file in it. A name
  // with no suffix, or one that is not a container suffix, is not offered - `--archive-any` is the way
  // to reach an archive whose name says nothing.
  constexpr std::array kNotOffered = std::to_array<std::string_view>({
      "walk.cc",
      "walk.h",
      "BUILD.bazel",
      "Makefile",
      "notes",
      "blob",
      "backup.dat",
      "a.tar.",
      ".gz",
  });
  for (const std::string_view name : kNotOffered) {
    EXPECT_THAT(LooksLikeContainerName(name), IsFalse()) << name;
  }
}

TEST_F(ArchiveBackendTest, ARegisteredOpenerIsUsedAndSeesTheMemberPathOptions) {
  // The options must reach the backend, or a container opened during a run would render member paths
  // with the default separator instead of the one the user asked for.
  std::string opened;
  std::string separator;
  RegisterContainerOpener(
      [&opened, &separator](std::string_view container, std::optional<std::string_view>, MemberPathOptions options) {
        opened = std::string(container);
        separator = std::string(options.separator);
        return std::make_unique<StubFileSystem>();
      });
  EXPECT_THAT(ContainerSupportAvailable(), IsTrue());
  const auto opened_fs = OpenContainer("a.tar", MemberPathOptions{.separator = "#"});
  ASSERT_THAT(opened_fs, ::mbo::testing::IsOk());
  EXPECT_THAT(opened_fs->get(), NotNull());
  EXPECT_THAT(opened, "a.tar");
  EXPECT_THAT(separator, "#");
}

TEST_F(ArchiveBackendTest, TheBackendsErrorReachesTheCallerUnchanged) {
  // "Not an archive" and "corrupt archive" must stay distinguishable through the seam: the walk treats
  // the first as an ordinary file and reports only the second, so a seam that flattened them would
  // turn every non-archive into an error.
  RegisterContainerOpener([](std::string_view container, std::optional<std::string_view>, MemberPathOptions) {
    if (container == "broken.tar") {
      return absl::StatusOr<std::unique_ptr<vfs::FileSystem>>(absl::DataLossError("corrupt"));
    }
    return absl::StatusOr<std::unique_ptr<vfs::FileSystem>>(absl::InvalidArgumentError("not an archive"));
  });
  EXPECT_THAT(OpenContainer("broken.tar"), StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(OpenContainer("notes.txt"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveBackendTest, OpenContainerBytesHandsTheContentToTheBackend) {
  // The nested-container path: there is no file to open, so the caller passes the bytes it already
  // read out of the parent and the label the members render under.
  std::string label;
  std::string content;
  RegisterContainerOpener(
      [&label, &content](std::string_view container, std::optional<std::string_view> bytes, MemberPathOptions) {
        label = std::string(container);
        content = bytes.has_value() ? std::string(*bytes) : std::string("<no bytes>");
        return std::make_unique<StubFileSystem>();
      });
  EXPECT_THAT(OpenContainerBytes("outer.tar!inner.tar", "TARBYTES"), ::mbo::testing::IsOk());
  EXPECT_THAT(label, "outer.tar!inner.tar");
  EXPECT_THAT(content, "TARBYTES");
  // And the path form still says "no bytes", so a backend can tell the two apart.
  EXPECT_THAT(OpenContainer("a.tar"), ::mbo::testing::IsOk());
  EXPECT_THAT(content, "<no bytes>");
}

TEST_F(ArchiveBackendTest, RegisteringAgainReplacesTheOpener) {
  // Last registration wins, which is what lets a test install a stub over whatever the binary linked.
  RegisterContainerOpener([](std::string_view, std::optional<std::string_view>, MemberPathOptions) {
    return absl::StatusOr<std::unique_ptr<vfs::FileSystem>>(absl::DataLossError("first"));
  });
  RegisterContainerOpener([](std::string_view, std::optional<std::string_view>, MemberPathOptions) {
    return absl::StatusOr<std::unique_ptr<vfs::FileSystem>>(absl::AbortedError("second"));
  });
  EXPECT_THAT(OpenContainer("a.tar"), StatusIs(absl::StatusCode::kAborted));
}

}  // namespace
}  // namespace xff::archive
