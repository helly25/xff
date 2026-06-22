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

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

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

// Per-evaluation environment threaded through Evaluate for one visited entry.
// Bundles what an expression node may read -- the entry, the action sink, the
// filesystem, the reference clock -- plus the traversal-control side-channel, so
// predicates and actions take them from one place rather than a long parameter
// list. Evaluation-wide options (e.g. modern -exec field substitution) attach
// here as the engine grows.
struct EvalContext {
  const Visit& visit;             // the entry being evaluated (constant across the expression)
  EmitFn emit;                    // action output sink (-print/-print0/...)
  const vfs::FileSystem& fs;      // backs predicates that read the source (e.g. -empty on a directory)
  absl::Time now;                 // single reference instant for age tests (-mtime/-mmin)
  Control& control;               // collects -prune/-quit requests
  bool exec_fields = false;       // --exec-fields: render -exec tokens through the field vocabulary
  std::vector<std::string>* captures = nullptr;  // -regex groups for gated -exec {0}..{N}; null when off
  const std::map<std::string, std::string>* defines = nullptr;  // --define values for {def.NAME}
  std::map<std::string, std::string>* outputs = nullptr;  // --capture results for {capture.NAME} (mutable, per entry)
  std::function<bool(std::string_view)> confirm;  // -ok prompt sink: returns true to run the command; empty -> decline
};

// Evaluates a parsed find expression against one visited entry and returns its
// overall truth value, mirroring find:
//   - tests: -name/-iname/-path/-ipath/-type/-true/-false against the entry;
//   - operators: !/-not, -a/-and, -o/-or, with short-circuit;
//   - actions: -print/-print0 write a record via `emit`; -prune/-quit set `control`.
// -name/-iname glob the basename, -path/-ipath glob the whole path; the `i`
// variants fold case (fnmatch, matching GNU find). Short-circuit means actions
// to the right of a failed -a (or in the unused branch of -o) do not fire.
// `context` carries the entry, the action sink, the filesystem, the reference
// clock, and the -prune/-quit side-channel (see EvalContext).
bool Evaluate(const parser::Expr& expr, EvalContext& context);

// True if `expr` contains any action node (-print, ...). The driver uses this
// to decide whether an implicit -print applies: find adds -print only when the
// expression has no action of its own.
bool ContainsAction(const parser::Expr& expr);

}  // namespace xff::engine

#endif  // XFF_ENGINE_EVALUATE_H_
