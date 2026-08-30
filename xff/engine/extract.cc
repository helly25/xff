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

#include "xff/engine/extract.h"

#include <sys/statvfs.h>
#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
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
#include "xff/env/env.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

namespace stdfs = ::std::filesystem;

// How much of a candidate directory's free space one member may take. A tmpfs is RAM shared with
// everything else on the machine, so filling it is worse than writing to disk; a quarter is generous
// for the "a member is small, the tmpfs is large" case this preference exists for, and refuses the
// pathological one.
constexpr std::uint64_t kMaxFreeSpaceShareDivisor = 4;

// The bytes free in `directory`, or nullopt when it cannot be queried (it does not exist, or is not
// readable) - which is also the answer for "do not use this candidate".
std::optional<std::uint64_t> FreeBytes(const std::string& directory) {
  struct ::statvfs stats{};
  if (::statvfs(directory.c_str(), &stats) != 0) {
    return std::nullopt;
  }
  // f_bavail is the space available to an unprivileged writer, which is what we are.
  return static_cast<std::uint64_t>(stats.f_bavail) * static_cast<std::uint64_t>(stats.f_frsize);
}

// Whether `directory` is a directory this process can create in.
bool Writable(const std::string& directory) {
  std::error_code error;
  return stdfs::is_directory(directory, error) && ::access(directory.c_str(), W_OK | X_OK) == 0;
}

// The member's own final component, which becomes the temporary file's name. Both separators are
// possible in one member path: the container is joined with `--archive-separator` (`!` by default)
// and the member's own directories with '/', as in `a.tar!dir/two.txt`.
std::string_view MemberName(std::string_view member) {
  const std::string_view::size_type sep = member.find_last_of("/!");
  return sep == std::string_view::npos ? member : member.substr(sep + 1);
}

}  // namespace

std::vector<std::string> DefaultExtractDirectories() {
  std::vector<std::string> candidates;
  // XDG_RUNTIME_DIR first: it is a tmpfs owned by this user (mode 0700), so it is both memory-backed
  // and private, where /dev/shm is world-writable and shared. Read through the env cache (xff/env),
  // which is the codebase's single getenv site.
  if (const std::optional<std::string> runtime_dir = env::Get("XDG_RUNTIME_DIR");
      runtime_dir.has_value() && !runtime_dir->empty()) {
    candidates.push_back(*runtime_dir);
  }
  candidates.emplace_back("/dev/shm");
  std::error_code error;
  const stdfs::path temporary = stdfs::temp_directory_path(error);
  candidates.push_back(error ? std::string("/tmp") : temporary.string());
  return candidates;
}

std::string ChooseExtractDirectory(std::uint64_t member_size, absl::Span<const std::string> candidates) {
  if (candidates.empty()) {
    std::error_code error;
    const stdfs::path temporary = stdfs::temp_directory_path(error);
    return error ? std::string("/tmp") : temporary.string();
  }
  for (const std::string& candidate : candidates.subspan(0, candidates.size() - 1)) {
    if (!Writable(candidate)) {
      continue;
    }
    const std::optional<std::uint64_t> free_bytes = FreeBytes(candidate);
    if (free_bytes.has_value() && member_size <= *free_bytes / kMaxFreeSpaceShareDivisor) {
      return candidate;
    }
  }
  // The last candidate is the fallback: it is taken whether the member fits or not, because there is
  // nowhere else to go and a write that fails says so with the real error.
  return candidates.back();
}

ExtractedMembers::~ExtractedMembers() {
  // The end-of-run sweep: a `+` batch runs after the walk, so its members are still held here, and a
  // child that died leaves its file behind too. Errors are ignored - there is no one left to tell,
  // and a temporary file that outlives the run is not worth failing over.
  for (const std::string& path : held_) {
    std::error_code error;
    // XFF_HOST_IO: extraction removes its explicitly selected temporary parent.
    stdfs::remove_all(stdfs::path(path).parent_path(), error);
  }
}

absl::StatusOr<std::string> ExtractedMembers::Extract(const vfs::FileSystem& fs, std::string_view member) {
  MBO_ASSIGN_OR_RETURN(const std::string content, fs.ReadContent(member));
  std::error_code error;
  // The directory is chosen per member, because the choice depends on the member's size (see
  // ChooseExtractDirectory): a small member goes to a memory-backed directory, a huge one to disk.
  const stdfs::path dir = stdfs::path(ChooseExtractDirectory(content.size(), DefaultExtractDirectories()))
                          / absl::StrCat("xff-", static_cast<std::int64_t>(::getpid()), "-", next_++);
  // XFF_HOST_IO: extraction creates its explicitly selected temporary directory.
  if (!stdfs::create_directory(dir, error)) {
    return absl::UnavailableError(absl::StrCat("cannot create ", dir.string(), ": ", error.message()));
  }
  const stdfs::path path = dir / std::string(MemberName(member));
  {
    // XFF_HOST_IO: extraction deliberately materializes a virtual member for an external process.
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
    if (!out) {
      // XFF_HOST_IO: extraction removes its explicitly selected temporary directory.
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
  // XFF_HOST_IO: extraction removes its explicitly selected temporary parent.
  stdfs::remove_all(stdfs::path(*it).parent_path(), error);
  held_.erase(it);
}

std::vector<std::string> ExtractedMembers::Held() const {
  return held_;
}

}  // namespace xff::engine
