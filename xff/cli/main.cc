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
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/local_fs.h"

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

  const absl::StatusOr<xff::parser::Command> command = xff::parser::Parse(args);
  if (!command.ok()) {
    std::cerr << "xff: " << command.status().message() << "\n";
    return 2;
  }

  // Walk the roots and evaluate the expression, printing matches. Per-path
  // errors -> exit 2 (the xff exit-code model; design.md "Exit-code model").
  const xff::vfs::LocalFs fs;
  const int errors = xff::engine::RunFind(
      *command, fs,
      [](std::string_view record) {
        std::cout.write(record.data(), static_cast<std::streamsize>(record.size()));
      },
      [](std::string_view path, absl::Status status) {
        std::cerr << "xff: " << path << ": " << status.message() << "\n";
      });
  return errors == 0 ? 0 : 2;
}
