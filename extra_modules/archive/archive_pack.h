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

#ifndef XFF_ARCHIVE_ARCHIVE_PACK_H_
#define XFF_ARCHIVE_ARCHIVE_PACK_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"

namespace xff::archive {

// One file to write into a new archive: where to read it from, and the name it gets INSIDE.
// Separating the two is the whole point - the walk knows the path on disk, the caller decides what
// the member should be called, and neither has to guess the other.
struct PackEntry {
  std::string source;  // the path on disk to read
  std::string name;    // the member name to store, exactly as given
};

// How the archive is written, beyond what the output name already decides. Empty means libarchive's
// defaults, which is what a caller that has no opinion should pass.
struct PackSettings {
  // The compressor's level, as libarchive's `compression-level`: gzip / bzip2 / xz take 0-9, zstd
  // 1-22, zip's deflate 0-9. Unset leaves the format's default. Setting it for a format with no
  // compressor (a plain `.tar`) is an error rather than a no-op, because a level that silently did
  // nothing would read as a smaller archive that never arrives.
  std::optional<int> level;
};

// Writes `entries` into a NEW archive at `path`, format chosen from the output NAME (see
// FormatFromName). This is the counterpart of the reader: xff walks, matches, and hands the result
// here, so an archive is built from an expression rather than from a file list piped through tar.
//
// Names are stored EXACTLY as given. No prefix is added or stripped, and no ordering is imposed:
// the caller has already decided both, because only it knows the roots the walk ran over and the
// order the user asked for (`--sort`).
//
// All or nothing, as the rewrite path is: the archive is built beside `path` and renamed over it
// only after every entry is written, so a failure part way leaves no half archive behind - and an
// existing file at `path` survives an attempt that fails.
//
// Errors: InvalidArgument when the output name carries no format this build can write; NotFound
// when a source path cannot be opened (nothing is written then, so a mistyped root cannot silently
// produce a partial archive); Unavailable when the temporary file or the rename fails.
[[nodiscard]] absl::Status PackFiles(
    std::string_view path,
    const std::vector<PackEntry>& entries,
    const PackSettings& options = {});

// The archive format an output NAME asks for, as a libarchive format+filter pair name ("tar.gz",
// "zip", ...), or empty when the name carries none. Public because the CLI validates the flag
// BEFORE the walk: spending a whole traversal to then report "I cannot write .rar" is the wrong
// order, and the accepted set belongs in one place rather than in a message.
[[nodiscard]] std::string FormatFromName(std::string_view path);

// Every output name FormatFromName accepts, in the order the help lists them. The SOT for both the
// check and the documentation, so they cannot disagree.
[[nodiscard]] std::vector<std::string> PackFormats();

}  // namespace xff::archive

#endif  // XFF_ARCHIVE_ARCHIVE_PACK_H_
