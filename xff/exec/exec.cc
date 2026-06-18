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

// posix_spawnp() and waitpid() are POSIX, hidden by glibc under the strict
// `-std=c++23` we build with; request them explicitly. No effect on macOS.
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#include "xff/exec/exec.h"

#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <string>
#include <string_view>
#include <vector>

// `environ` is the process environment handed to spawned children -- a global
// provided by the C runtime. Declared directly for portability (macOS does not
// expose it via <unistd.h>); a compatible redeclaration where it already is.
extern "C" char** environ;

namespace xff::exec {
namespace {

// Replaces every "{}" in `token` with `path` (find's -exec placeholder).
std::string SubstitutePlaceholder(std::string_view token, std::string_view path) {
  std::string out;
  for (std::string_view::size_type i = 0; i < token.size();) {
    if (i + 1 < token.size() && token[i] == '{' && token[i + 1] == '}') {
      out.append(path);
      i += 2;
    } else {
      out.push_back(token[i]);
      ++i;
    }
  }
  return out;
}

}  // namespace

bool Execute(const std::vector<std::string>& command, std::string_view path) {
  if (command.empty()) {
    return false;
  }
  std::vector<std::string> args;
  args.reserve(command.size());
  for (const std::string& token : command) {
    args.push_back(SubstitutePlaceholder(token, path));
  }
  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (std::string& arg : args) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);

  pid_t pid = 0;
  if (::posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0) {
    return false;  // could not spawn (e.g. command not found)
  }
  int status = 0;
  if (::waitpid(pid, &status, 0) != pid) {
    return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}  // namespace xff::exec
