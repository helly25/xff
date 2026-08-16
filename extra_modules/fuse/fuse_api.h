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

#ifndef XFF_FUSE_FUSE_API_H_
#define XFF_FUSE_FUSE_API_H_

// The typed fuse3 call surface (epic #183, slice 3a): every symbol the loader resolved, cast ONCE
// into the real function type from libfuse's own headers - the fetched release the module pins, so
// `fuse_lowlevel_ops`'s layout and `fuse_session_new`'s signature are libfuse's, never transcribed.
// The mount server (slice 3b) calls through this struct; nothing else in the module casts.

#include <cstddef>

#include "absl/status/statusor.h"
#include "fuse_lowlevel.h"  // @libfuse//:fuse3_headers, interface-only (see libfuse.BUILD.bazel)

namespace xff::fuse {

struct FuseApi {
  // The one constructor: resolves every field through FuseLoader. Unavailable FUSE (no library, or
  // one lacking a symbol) is the loader's error, verbatim - the caller turns it into the degrade.
  static absl::StatusOr<FuseApi> Resolve();

  struct fuse_session* (*session_new)(
      struct fuse_args* args,
      const struct fuse_lowlevel_ops* op,
      std::size_t op_size,
      void* userdata) = nullptr;
  int (*session_mount)(struct fuse_session* se, const char* mountpoint) = nullptr;
  int (*session_loop)(struct fuse_session* se) = nullptr;
  void (*session_exit)(struct fuse_session* se) = nullptr;
  void (*session_unmount)(struct fuse_session* se) = nullptr;
  void (*session_destroy)(struct fuse_session* se) = nullptr;

  std::size_t (*add_direntry)(
      fuse_req_t req,
      char* buf,
      std::size_t bufsize,
      const char* name,
      const struct stat* stbuf,
      off_t off) = nullptr;
  int (*reply_err)(fuse_req_t req, int err) = nullptr;
  int (*reply_attr)(fuse_req_t req, const struct stat* attr, double attr_timeout) = nullptr;
  int (*reply_entry)(fuse_req_t req, const struct fuse_entry_param* entry) = nullptr;
  int (*reply_buf)(fuse_req_t req, const char* buf, std::size_t size) = nullptr;
  int (*reply_open)(fuse_req_t req, const struct fuse_file_info* file_info) = nullptr;
  int (*reply_readlink)(fuse_req_t req, const char* link) = nullptr;
  void* (*req_userdata)(fuse_req_t req) = nullptr;
};

}  // namespace xff::fuse

#endif  // XFF_FUSE_FUSE_API_H_
