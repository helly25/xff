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

#ifndef XFF_ENGINE_RUN_H_
#define XFF_ENGINE_RUN_H_

#include "xff/engine/evaluate.h"
#include "xff/engine/walk.h"
#include "xff/parser/ast.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {

// Runs a parsed find command over `fs`: walks the roots in pre-order and, for
// each entry, evaluates the expression -- firing -print/-print0 actions through
// `emit`. When the expression has no action of its own (or is empty), matching
// entries get an implicit -print. Per-path failures are reported to `on_error`.
//
// Returns the number of per-path errors encountered (0 == clean). The CLI maps
// a nonzero count to exit 2; the full exit-code model is a follow-up.
int RunFind(const parser::Command& command, const vfs::FileSystem& fs, EmitFn emit, WalkErrorFn on_error);

}  // namespace xff::engine

#endif  // XFF_ENGINE_RUN_H_
