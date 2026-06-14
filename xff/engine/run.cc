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

#include "xff/engine/run.h"

#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/walk.h"
#include "xff/parser/ast.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {

int RunFind(const parser::Command& command, const vfs::FileSystem& fs, EmitFn emit, WalkErrorFn on_error) {
  const parser::Expr* const expression = command.expression.get();
  const bool has_action = expression != nullptr && ContainsAction(*expression);
  int errors = 0;

  const absl::Status status = Walk(
      fs, command.roots, WalkOptions{},
      [&](const Visit& visit) {
        const bool matched = expression == nullptr || Evaluate(*expression, visit, emit);
        if (matched && !has_action) {
          emit(absl::StrCat(visit.path, "\n"));  // implicit -print
        }
        return WalkAction::kContinue;
      },
      [&](std::string_view path, const absl::Status& error_status) {
        ++errors;
        on_error(path, error_status);
      });
  if (!status.ok()) {
    ++errors;  // Fatal traversal error (none today; per-path errors handled above).
  }

  return errors;
}

}  // namespace xff::engine
