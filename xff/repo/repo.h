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

#ifndef XFF_REPO_REPO_H_
#define XFF_REPO_REPO_H_

#include <optional>
#include <string>
#include <string_view>

#include "xff/vfs/filesystem.h"

namespace xff::repo {

// Walks up from `start_dir` (inclusive) toward the filesystem root, returning the
// first ancestor directory that contains a `.git` entry -- the git working-tree
// root. A `.git` directory (a normal checkout) and a `.git` file (a worktree or
// submodule gitdir pointer) both count, so existence is enough; the contents are
// not inspected. Returns nullopt when no ancestor has a `.git`.
//
// `start_dir` must be an absolute, normalized path ("/a/b", no trailing slash
// except the root "/"); callers resolve relative roots first. Existence is probed
// through `fs` (Stat, no symlink follow), so a fake filesystem drives the tests.
std::optional<std::string> FindRepoRoot(const vfs::FileSystem& fs, std::string_view start_dir);

}  // namespace xff::repo

#endif  // XFF_REPO_REPO_H_
