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

#include <memory>
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

TEST_F(ArchiveBackendTest, ARegisteredOpenerIsUsedAndSeesTheMemberPathOptions) {
  // The options must reach the backend, or a container opened during a run would render member paths
  // with the default separator instead of the one the user asked for.
  std::string opened;
  std::string separator;
  RegisterContainerOpener([&opened, &separator](std::string_view container, MemberPathOptions options) {
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
  RegisterContainerOpener([](std::string_view container, MemberPathOptions) {
    if (container == "broken.tar") {
      return absl::StatusOr<std::unique_ptr<vfs::FileSystem>>(absl::DataLossError("corrupt"));
    }
    return absl::StatusOr<std::unique_ptr<vfs::FileSystem>>(absl::InvalidArgumentError("not an archive"));
  });
  EXPECT_THAT(OpenContainer("broken.tar"), StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_THAT(OpenContainer("notes.txt"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ArchiveBackendTest, RegisteringAgainReplacesTheOpener) {
  // Last registration wins, which is what lets a test install a stub over whatever the binary linked.
  RegisterContainerOpener([](std::string_view, MemberPathOptions) {
    return absl::StatusOr<std::unique_ptr<vfs::FileSystem>>(absl::DataLossError("first"));
  });
  RegisterContainerOpener([](std::string_view, MemberPathOptions) {
    return absl::StatusOr<std::unique_ptr<vfs::FileSystem>>(absl::AbortedError("second"));
  });
  EXPECT_THAT(OpenContainer("a.tar"), StatusIs(absl::StatusCode::kAborted));
}

}  // namespace
}  // namespace xff::archive
