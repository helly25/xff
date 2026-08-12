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

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "mbo/status/status_macros.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

namespace stdfs = std::filesystem;

// The member's own final component, which becomes the temporary file's name. Both separators are
// possible in one member path: the container is joined with `--archive-separator` (`!` by default)
// and the member's own directories with '/', as in `a.tar!dir/two.txt`.
std::string_view MemberName(std::string_view member) {
  const std::string_view::size_type sep = member.find_last_of("/!");
  return sep == std::string_view::npos ? member : member.substr(sep + 1);
}

}  // namespace

ExtractedMembers::~ExtractedMembers() {
  // The end-of-run sweep: a `+` batch runs after the walk, so its members are still held here, and a
  // child that died leaves its file behind too. Errors are ignored - there is no one left to tell,
  // and a temporary file that outlives the run is not worth failing over.
  for (const std::string& path : held_) {
    std::error_code error;
    stdfs::remove_all(stdfs::path(path).parent_path(), error);
  }
}

absl::StatusOr<std::string> ExtractedMembers::Extract(const vfs::FileSystem& fs, std::string_view member) {
  MBO_ASSIGN_OR_RETURN(const std::string content, fs.ReadContent(member));
  std::error_code error;
  const stdfs::path dir =
      stdfs::temp_directory_path(error) / absl::StrCat("xff-", static_cast<long>(::getpid()), "-", next_++);
  if (error) {
    return absl::UnavailableError(absl::StrCat("no temporary directory for an extracted member: ", error.message()));
  }
  if (!stdfs::create_directory(dir, error)) {
    return absl::UnavailableError(absl::StrCat("cannot create ", dir.string(), ": ", error.message()));
  }
  const stdfs::path path = dir / std::string(MemberName(member));
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
    if (!out) {
      stdfs::remove_all(dir, error);
      return absl::UnavailableError(absl::StrCat("cannot write the extracted member to ", path.string()));
    }
  }
  // Owner-only, because the content came out of the user's archive and a shared temporary directory
  // is world-readable by default.
  stdfs::permissions(path, stdfs::perms::owner_read | stdfs::perms::owner_write, error);
  held_.push_back(path.string());
  return held_.back();
}

void ExtractedMembers::Release(std::string_view path) {
  const auto it = absl::c_find(held_, path);
  if (it == held_.end()) {
    return;
  }
  std::error_code error;
  stdfs::remove_all(stdfs::path(*it).parent_path(), error);
  held_.erase(it);
}

std::vector<std::string> ExtractedMembers::Held() const {
  return held_;
}

}  // namespace xff::engine
