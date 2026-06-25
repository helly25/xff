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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xff/config/config.h"
#include "xff/config/loader.h"
#include "xff/config/policy.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/registry/descriptor.h"
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
  const std::ifstream in{std::string(path), std::ios::binary};
  if (!in) {
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// The absolute directories whose .xffrc ancestor chains form the project cascade:
// each search root resolved to an absolute path (a regular file -> its parent
// directory), or the current directory when no roots were given. The loader walks
// each one's ancestors; a non-existent root still contributes its ancestor chain.
std::vector<std::string> ProjectDirs(const std::vector<std::string>& roots) {
  namespace fs = std::filesystem;
  std::vector<std::string> inputs = roots;
  if (inputs.empty()) {
    inputs.emplace_back(".");
  }
  std::vector<std::string> dirs;
  dirs.reserve(inputs.size());
  for (const std::string& root : inputs) {
    std::error_code ec;
    fs::path abs = fs::absolute(root, ec);
    if (ec) {
      continue;
    }
    abs = abs.lexically_normal();
    if (fs::is_regular_file(abs, ec)) {
      abs = abs.parent_path();
    }
    dirs.push_back(abs.string());
  }
  return dirs;
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

  absl::StatusOr<xff::parser::Command> parsed = xff::parser::Parse(args);
  if (!parsed.ok()) {
    std::cerr << "xff: " << parsed.status().message() << "\n";
    return 2;
  }
  xff::parser::Command command = *std::move(parsed);

  // Load the layered config (system + user + explicit --xffrc) and resolve the
  // effective flags. --explain writes that effective configuration and exits.
  xff::config::DiscoveryOptions opts = xff::config::SelectorsFromGlobals(command.globals);
  // argv[0] dispatch: the program name picks the base style (invoked as `find` ->
  // strict find; as `xff` or any other alias -> modern xff) as the lowest-precedence
  // selector, so an explicit --config still overrides it (design-config.md "CLI
  // selectors"). Prepended before discovery so find:/xff: .xffrc lines gate on it too.
  opts.configs.insert(opts.configs.begin(), std::string(xff::config::DefaultStyleForProgram(argv[0])));
  opts.xff_config = EnvOpt("XFF_CONFIG");
  opts.xdg_config_home = EnvOpt("XDG_CONFIG_HOME");
  opts.home = EnvOpt("HOME");
  opts.roots = ProjectDirs(command.roots);  // absolute dirs for the project .xffrc cascade
  const xff::config::ConfigInputs inputs = xff::config::Discover(opts, ReadFile);
  std::vector<xff::config::Drop> drops;
  const xff::config::ConfigInputs gated = xff::config::GateConfig(inputs, &drops);
  const std::vector<xff::config::ResolvedFlag> resolved = xff::config::ResolveConfig(gated);
  if (absl::c_contains(command.globals, "--explain")) {
    std::cout << xff::config::ExplainSources(inputs.sources, xff::config::ActiveStyle(inputs.configs));
    std::cout << xff::config::ExplainConfig(resolved, command.globals);
    for (const xff::config::Drop& drop : drops) {
      std::cout << "dropped\t" << xff::config::DropMessage(drop) << "\n";
    }
    return 0;
  }
  // A disallowed config line is dropped, never fatal: warn (self-documenting) and
  // carry on with the survivors (design-config.md "Enforcement & self-documentation").
  for (const xff::config::Drop& drop : drops) {
    std::cerr << "xff: ignoring " << xff::config::DropMessage(drop) << " - denied by config policy\n";
  }
  // Apply the config: prepend the resolved flags to the globals so they take
  // effect, the CLI globals (already present, kept last) winning on conflict.
  std::vector<std::string> config_flags;
  config_flags.reserve(resolved.size());
  for (const xff::config::ResolvedFlag& flag : resolved) {
    config_flags.push_back(flag.flag);
  }
  command.globals.insert(command.globals.begin(), config_flags.begin(), config_flags.end());

  // The strict find style (--config=find) accepts only find's own vocabulary;
  // reject xff extensions (e.g. -println) so a find-style run behaves like GNU
  // find (design-config.md "CLI selectors"). The default xff style accepts all.
  const xff::registry::Style style = xff::config::ActiveStyle(inputs.configs);
  if (const absl::Status status = xff::parser::EnforceStyle(command, style); !status.ok()) {
    std::cerr << "xff: " << status.message() << "\n";
    return 2;
  }

  // Walk the roots and evaluate the expression, printing matches. Per-path
  // errors -> exit 2 (the xff exit-code model; design.md "Exit-code model").
  const xff::vfs::LocalFs fs;
  const int errors = xff::engine::RunFind(
      command, fs,
      [](std::string_view record) { std::cout.write(record.data(), static_cast<std::streamsize>(record.size())); },
      [](std::string_view path, absl::Status status) {
        std::cerr << "xff: " << path << ": " << status.message() << "\n";
      });
  return errors == 0 ? 0 : 2;
}
