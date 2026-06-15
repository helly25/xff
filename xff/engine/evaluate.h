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

#ifndef XFF_ENGINE_EVALUATE_H_
#define XFF_ENGINE_EVALUATE_H_

#include <string_view>

#include "absl/functional/function_ref.h"
#include "absl/time/time.h"
#include "xff/engine/walk.h"
#include "xff/parser/ast.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {

// Output sink for actions: receives one fully-formed record per action firing
// (path plus terminator -- "p\n" for -print, "p\0" for -print0).
using EmitFn = absl::FunctionRef<void(std::string_view)>;

// Traversal-control side-channel, set by control actions during evaluation and
// read by the driver: -prune asks the walk not to descend into the current
// directory; -quit asks it to stop the traversal entirely. Like `emit`, these
// fire as a side effect of reaching the action node, so short-circuit governs
// whether they are set.
struct Control {
  bool prune = false;
  bool quit = false;
};

// Evaluates a parsed find expression against one visited entry and returns its
// overall truth value, mirroring find:
//   - tests: -name/-iname/-path/-ipath/-type/-true/-false against the entry;
//   - operators: !/-not, -a/-and, -o/-or, with short-circuit;
//   - actions: -print/-print0 write a record via `emit`; -prune/-quit set `control`.
// -name/-iname glob the basename, -path/-ipath glob the whole path; the `i`
// variants fold case (fnmatch, matching GNU find). Short-circuit means actions
// to the right of a failed -a (or in the unused branch of -o) do not fire.
// `fs` backs predicates that must read the source (e.g. -empty on a directory).
// `now` is the single reference instant for age tests (-mtime/-mmin); the driver
// captures it once so every entry is compared against the same clock, like find.
// `control` collects traversal-control requests raised by -prune/-quit.
bool Evaluate(
    const parser::Expr& expr, const Visit& visit, EmitFn emit, const vfs::FileSystem& fs, absl::Time now,
    Control& control);

// True if `expr` contains any action node (-print, ...). The driver uses this
// to decide whether an implicit -print applies: find adds -print only when the
// expression has no action of its own.
bool ContainsAction(const parser::Expr& expr);

}  // namespace xff::engine

#endif  // XFF_ENGINE_EVALUATE_H_
