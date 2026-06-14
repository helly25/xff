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

#include <iostream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xff/parser/parser.h"

int main(int argc, char** argv) {
  const std::vector<std::string> args(argv + 1, argv + argc);

  for (const std::string& arg : args) {
    if (arg == "--help" || arg == "-h") {
      std::cout << "xff -- eXtended File Find (skeleton)\n"
                   "usage: xff [globals] <dir...> [find expression]\n";
      return 0;
    }
    if (arg == "--version") {
      std::cout << "xff 0.0.0\n";
      return 0;
    }
  }

  // Skeleton behaviour: parse and echo the search roots. Errors -> exit 2
  // (the xff exit-code model; design.md "Exit-code model").
  const absl::StatusOr<xff::parser::Command> cmd = xff::parser::Parse(args);
  if (!cmd.ok()) {
    std::cerr << "xff: " << cmd.status().message() << "\n";
    return 2;
  }
  for (const std::string& root : cmd->roots) {
    std::cout << root << "\n";
  }
  return 0;
}
