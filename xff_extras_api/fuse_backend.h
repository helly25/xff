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

#ifndef XFF_FUSE_FUSE_BACKEND_H_
#define XFF_FUSE_FUSE_BACKEND_H_

// The FUSE extra's registration slot (epic #183): the core asks "is the mount extra LINKED into
// this binary" through here, the same question the regex slot answers with `Pcre2Available()`.
// Linked is a build-time fact and is deliberately distinct from MOUNTABLE: whether the machine's
// runtime fuse3 library exists is probed per run by the extra itself, and its absence is a degrade
// (extraction fallback), never a missing feature. `--help=extras` and the notice line report the
// build-time fact, so they read the slot.
//
// The mount entry points themselves stay in the extra (@xff_fuse); this seam will grow the typed
// mount factory when the CLI flag lands (#183 slice 4b).

namespace xff::fuse {

// Records that the FUSE mount extra is linked in. Called once, at static-init, from the extra's
// registration translation unit (which must be alwayslink so the registrar is not dropped).
// Static-init only; not thread-safe.
void RegisterMountSupport();

// Self-registers on construction. Declare one at namespace scope in the extra's registration TU:
//   const xff::fuse::MountSupportRegistrar kRegisterFuseMount{};
struct MountSupportRegistrar {
  MountSupportRegistrar() { RegisterMountSupport(); }
};

// Whether the FUSE mount extra is compiled into this binary. False in the lean build; true when
// @xff_fuse's registration TU is linked (--//xff:xff_fuse / --//xff:xff_all).
[[nodiscard]] bool MountSupportAvailable();

}  // namespace xff::fuse

#endif  // XFF_FUSE_FUSE_BACKEND_H_
