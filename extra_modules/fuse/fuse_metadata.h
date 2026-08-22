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

#ifndef XFF_FUSE_FUSE_METADATA_H_
#define XFF_FUSE_FUSE_METADATA_H_

#include <sys/stat.h>

#include <cstdint>

#include "absl/status/status.h"
#include "xff/vfs/entry.h"

namespace xff::fuse {

// Translates VFS failures and metadata into the FUSE/kernel representation. These are pure policy
// functions, separate from the request callbacks that send their results through libfuse.
int ErrnoForStatus(absl::StatusCode code);
mode_t ModeBitsForFileType(vfs::FileType type);
struct stat StatForMetadata(const vfs::Metadata& metadata, std::uint64_t ino);

}  // namespace xff::fuse

#endif  // XFF_FUSE_FUSE_METADATA_H_
