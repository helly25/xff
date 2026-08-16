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

// Linking fuse_register_cc (as any binary carrying the fuse extra does) must fill BOTH identity
// surfaces: the extras slot the core's `--help=extras` / notice line read, and the libfuse notice.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/fuse/fuse_backend.h"
#include "xff/license/notice.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::fuse {
namespace {

using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsTrue;
using ::testing::Not;

// The factory takes a filesystem; what a mount DOES with one is the server's test. This is the
// smallest thing that satisfies the interface.
struct FakeFileSystem : vfs::FileSystem {
  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view /*dir*/) const override {
    return std::vector<vfs::Entry>{};
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view /*path*/, bool /*follow_symlinks*/) const override {
    return vfs::Metadata{.type = vfs::FileType::kDirectory};
  }

  absl::Status Remove(std::string_view /*path*/) const override { return absl::UnimplementedError("read-only"); }

  bool Access(std::string_view /*path*/, vfs::AccessMode /*mode*/) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view /*path*/) const override {
    return absl::InvalidArgumentError("not a symlink");
  }

  absl::StatusOr<std::string> FsType(std::string_view /*path*/) const override { return std::string("fake"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view /*path*/) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view /*path*/) const override {
    return absl::NotFoundError("no content");
  }
};

struct FuseRegisterTest : ::testing::Test {};

TEST_F(FuseRegisterTest, LinkingRegistersMountSupport) {
  EXPECT_THAT(MountSupportAvailable(), IsTrue());
}

TEST_F(FuseRegisterTest, LinkingRegistersTheMountFactory) {
  // The slot answers "is the extra linked"; the FACTORY is what makes a mount possible, and the
  // same translation unit registers both on purpose - a binary that advertises mounting but cannot
  // mount would be worse than one that advertises nothing. Whether a mount SUCCEEDS depends on the
  // machine (fuse3 present, /dev/fuse usable), so what is asserted here is only that the call
  // reached a real factory instead of the "not built in" stub.
#if defined(MEMORY_SANITIZER)
  // Reaching the real factory means reaching the dlopened SYSTEM libfuse3, which MSan did not
  // instrument - so every byte it writes reads back as uninitialized. Same reason the server test
  // and the CLI mount test skip here; ASan and TSan run this path.
  GTEST_SKIP() << "MSan cannot model the uninstrumented system libfuse3";
#else
  EXPECT_THAT(
      MountContainer(std::make_shared<FakeFileSystem>(), "/container.tar"),
      Not(StatusIs(absl::StatusCode::kUnimplemented)));
#endif
}

TEST_F(FuseRegisterTest, LinkingRegistersTheLibfuseNotice) {
  EXPECT_THAT(
      license::Notices(), Contains(AllOf(
                              Field("component", &license::Notice::component, "libfuse"),
                              Field("spdx", &license::Notice::spdx, "LGPL-2.1-only"),
                              Field("text", &license::Notice::text, HasSubstr("interface-only")))));
}

}  // namespace
}  // namespace xff::fuse
