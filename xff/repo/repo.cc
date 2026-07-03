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

#include "xff/repo/repo.h"

#include <optional>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "xff/vfs/filesystem.h"

namespace xff::repo {

std::optional<std::string> FindRepoRoot(const vfs::FileSystem& fs, std::string_view start_dir) {
  std::string dir(start_dir);
  while (!dir.empty()) {
    // Probe `<dir>/.git` (avoid a "//" when dir is the root "/"). Stat succeeds for
    // both a `.git` directory and a `.git` file, which is exactly what we want.
    const std::string dot_git = dir == "/" ? "/.git" : absl::StrCat(dir, "/.git");
    if (fs.Stat(dot_git, /*follow_symlinks=*/false).ok()) {
      return dir;
    }
    if (dir == "/") {
      break;  // reached the filesystem root without a hit
    }
    const std::string::size_type slash = dir.rfind('/');
    // Parent of "/foo" is "/"; parent of "/a/b" is "/a". A path with no slash is
    // not absolute (the caller's contract), so stop rather than loop forever.
    dir = slash == std::string::npos ? std::string() : (slash == 0 ? std::string("/") : dir.substr(0, slash));
  }
  return std::nullopt;
}

}  // namespace xff::repo
