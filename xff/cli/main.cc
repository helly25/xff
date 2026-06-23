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

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xff/config/config.h"
#include "xff/config/loader.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/local_fs.h"

namespace {

// Environment variable as an optional (nullopt when unset), for config discovery.
std::optional<std::string> EnvOpt(const char* name) {
  const char* const value = std::getenv(name);
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::string(value);
}

// Reads a whole file, or nullopt if it cannot be opened: the config FileReader.
std::optional<std::string> ReadFile(std::string_view path) {
  std::ifstream in{std::string(path), std::ios::binary};
  if (!in) {
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

}  // namespace

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

  // Load the layered config (system + user + explicit --xffrc) and resolve the
  // effective flags. --explain writes that effective configuration and exits;
  // applying it to the run (prepending to the globals) is the next slice.
  xff::config::DiscoveryOptions opts = xff::config::SelectorsFromGlobals(command->globals);
  opts.xff_config = EnvOpt("XFF_CONFIG");
  opts.xdg_config_home = EnvOpt("XDG_CONFIG_HOME");
  opts.home = EnvOpt("HOME");
  const std::vector<xff::config::ResolvedFlag> resolved =
      xff::config::ResolveConfig(xff::config::Discover(opts, ReadFile));
  if (absl::c_contains(command->globals, "--explain")) {
    std::cout << xff::config::ExplainConfig(resolved, command->globals);
    return 0;
  }

  // Walk the roots and evaluate the expression, printing matches. Per-path
  // errors -> exit 2 (the xff exit-code model; design.md "Exit-code model").
  const xff::vfs::LocalFs fs;
  const int errors = xff::engine::RunFind(
      *command, fs,
      [](std::string_view record) { std::cout.write(record.data(), static_cast<std::streamsize>(record.size())); },
      [](std::string_view path, absl::Status status) {
        std::cerr << "xff: " << path << ": " << status.message() << "\n";
      });
  return errors == 0 ? 0 : 2;
}
