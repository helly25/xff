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

// The FUSE extra's identity: linking this translation unit IS what "the fuse extra is compiled in"
// means (epic #183 slice 4a). It fills the xff_extras_api slot the core reads for `--help=extras` /
// the notice line, and registers the libfuse notice. The target is alwayslink so neither file-scope
// registrar is dropped.

#include "xff/fuse/fuse_backend.h"
#include "xff/fuse/fuse_server.h"  // IWYU pragma: keep - links the server the extra exists to provide
#include "xff/license/notice.h"

namespace xff::fuse {
namespace {

const MountSupportRegistrar kRegisterFuseMount{};

// Interface-only use: the binary compiles against libfuse's headers (fetched, pinned release) and
// dlopens the SYSTEM's libfuse3 at runtime; no LGPL code is compiled or statically linked in. The
// notice credits the interface and says where the library itself lives.
const license::Registrar kLibfuseNotice{{
    .component = "libfuse",
    .spdx = "LGPL-2.1-only",
    .text = "libfuse (https://github.com/libfuse/libfuse), Copyright (c) Miklos Szeredi and contributors.\n"
            "Used interface-only: compiled against its headers; the library itself is loaded from the\n"
            "host system at runtime (dlopen) and is not distributed with this binary.",
}};

}  // namespace
}  // namespace xff::fuse
