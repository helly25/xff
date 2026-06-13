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

#include "xff/parser/parser.h"

#include <cstddef>
#include <string>
#include <vector>

namespace xff::parser {
namespace {

// An expression begins at the first find operator/predicate token: a
// single-dash word, or one of '(' ')' '!' ','.
bool StartsExpression(const std::string& arg) {
  if (arg.empty()) {
    return false;
  }
  if (arg[0] == '-') {
    return true;
  }
  return arg == "(" || arg == ")" || arg == "!" || arg == ",";
}

}  // namespace

Command Parse(const std::vector<std::string>& args) {
  Command cmd;
  std::size_t i = 0;

  // Globals: leading '-'/'+' tokens before the first root; '--' ends the
  // global region explicitly.
  for (; i < args.size(); ++i) {
    const std::string& arg = args[i];
    if (arg == "--") {
      ++i;
      break;
    }
    if (!arg.empty() && (arg[0] == '-' || arg[0] == '+')) {
      cmd.globals.push_back(arg);
    } else {
      break;
    }
  }

  // Roots: operands until the expression begins.
  for (; i < args.size(); ++i) {
    if (StartsExpression(args[i])) {
      break;
    }
    cmd.roots.push_back(args[i]);
  }

  // Remainder: the find expression (full parse comes later).
  for (; i < args.size(); ++i) {
    cmd.expression.push_back(args[i]);
  }

  return cmd;
}

}  // namespace xff::parser
