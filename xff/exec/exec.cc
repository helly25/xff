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
# define _GNU_SOURCE 1
#endif

#include "xff/exec/exec.h"

#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <optional>
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

bool ExecuteArgs(const std::vector<std::string>& args) {
  if (args.empty()) {
    return false;
  }
  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (const std::string& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));  // posix_spawnp wants char* const*; it does not modify argv
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

bool Execute(const std::vector<std::string>& command, std::string_view path) {
  std::vector<std::string> args;
  args.reserve(command.size());
  for (const std::string& token : command) {
    args.push_back(SubstitutePlaceholder(token, path));
  }
  return ExecuteArgs(args);  // empty command -> empty args -> false
}

std::optional<std::string> CaptureOutput(const std::vector<std::string>& args) {
  if (args.empty()) {
    return std::nullopt;
  }
  int pipe_fds[2] = {-1, -1};
  if (::pipe(pipe_fds) != 0) {
    return std::nullopt;
  }

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);  // child stdout -> pipe write end
  posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);                // child needs neither raw fd
  posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);

  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (const std::string& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));  // posix_spawnp wants char* const*; it does not modify argv
  }
  argv.push_back(nullptr);

  pid_t pid = 0;
  const int spawned = ::posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  ::close(pipe_fds[1]);  // parent drops the write end so read() sees EOF once the child exits
  if (spawned != 0) {
    ::close(pipe_fds[0]);
    return std::nullopt;  // could not spawn (e.g. command not found)
  }

  // Drain the pipe before waiting, so a child writing more than the pipe buffer
  // cannot deadlock against our waitpid.
  std::string output;
  char buffer[4'096];
  for (;;) {
    const ssize_t n = ::read(pipe_fds[0], buffer, sizeof(buffer));
    if (n > 0) {
      output.append(buffer, static_cast<std::size_t>(n));
    } else if (n == 0 || errno != EINTR) {
      break;  // EOF, or a non-retryable read error
    }
  }
  ::close(pipe_fds[0]);
  int status = 0;
  ::waitpid(pid, &status, 0);  // reap; the capture value is the stdout, not the exit code
  return output;
}

}  // namespace xff::exec
