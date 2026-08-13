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

#include "xff/cli/pager.h"

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "xff/env/env.h"

namespace xff::cli {
namespace {

// Writes `text` straight to stdout - the fallback whenever paging is off or fails.
void WriteToStdout(std::string_view text) {
  std::cout << text;
}

// Pipes `text` through `command` (run via `sh -c`, so args and pipelines work), with
// `text` on the child's stdin. Returns false if the pager could not be started (the
// caller then falls back to stdout); a pager that starts and then exits early is a
// success (the user quit less), not a failure.
[[nodiscard]] bool PipeThroughPager(std::string_view text, const std::string& command) {
  std::array<int, 2> fds{};
  // macOS has no pipe2(), so the CLOEXEC flag is not available here; the child closes
  // both raw ends explicitly before exec, so nothing leaks.
  if (::pipe(fds.data()) != 0) {  // NOLINT(android-cloexec-pipe)
    return false;
  }
  const int read_fd = fds[0];
  const int write_fd = fds[1];
  const ::pid_t pid = ::fork();
  if (pid < 0) {
    ::close(read_fd);
    ::close(write_fd);
    return false;
  }
  if (pid == 0) {
    // Child: the pager reads our text as its stdin, then runs on the inherited stdout.
    ::dup2(read_fd, STDIN_FILENO);
    ::close(read_fd);
    ::close(write_fd);
    // execlp needs a C string at the exec boundary; `sh -c` handles args / pipelines.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ::execlp("sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
    ::_exit(127);  // exec failed: nothing was consumed yet, so the parent still has the text
  }
  // Parent: feed the text to the pager, ignoring SIGPIPE so an early quit (a closed read
  // end) surfaces as a write error we can stop on rather than killing the process.
  ::close(read_fd);
  struct ::sigaction ignore{};
  struct ::sigaction previous{};
  ignore.sa_handler = SIG_IGN;
  ::sigaction(SIGPIPE, &ignore, &previous);
  std::size_t written = 0;
  while (written < text.size()) {
    const std::string_view remaining = text.substr(written);
    const ::ssize_t wrote = ::write(write_fd, remaining.data(), remaining.size());
    if (wrote <= 0) {
      break;  // the pager closed its input (e.g. user quit less before the end)
    }
    written += static_cast<std::size_t>(wrote);
  }
  ::close(write_fd);
  ::sigaction(SIGPIPE, &previous, nullptr);
  int status = 0;
  ::waitpid(pid, &status, 0);
  // exec failure is our only "could not start" signal; the text was not consumed, so let
  // the caller re-emit it to stdout.
  return WIFEXITED(status) == 0 || WEXITSTATUS(status) != 127;
}

}  // namespace

PagerWhen ResolvePagerWhen(const std::vector<std::string>& args) {
  PagerWhen when = PagerWhen::kAuto;
  for (const std::string_view arg : args) {
    if (arg == "--no-pager" || arg == "--pager=never") {
      when = PagerWhen::kNever;
    } else if (arg == "--pager" || arg == "--pager=always") {
      when = PagerWhen::kAlways;
    } else if (arg == "--pager=auto") {
      when = PagerWhen::kAuto;
    } else if (arg == "--pager=all") {
      when = PagerWhen::kAll;
    }
  }
  return when;
}

namespace {

// The text pager: $XFF_PAGER, else $PAGER, else the built-in. A variable that is set
// (even to "") is authoritative, so an empty value means "no pager".
std::string ResolveTextPager() {
  if (const std::optional<std::string> xff_pager = env::Get("XFF_PAGER")) {
    return *xff_pager;
  }
  if (const std::optional<std::string> pager = env::Get("PAGER")) {
    return *pager;
  }
  return "less -FRX";
}

}  // namespace

std::string ResolvePagerCommand(PagerKind kind) {
  if (kind == PagerKind::kMan) {
    // $XFF_MANPAGER wins outright (empty disables), so a user can plug in any roff
    // viewer, e.g. `groff -mandoc -Tutf8 | less -R`.
    if (const std::optional<std::string> man_pager = env::Get("XFF_MANPAGER")) {
      return *man_pager;
    }
    // The built-in formats the roff with mandoc (the portable roff formatter --man's own
    // help points at) and pages it, honoring $PAGER like man does, else less -FRX. If
    // mandoc is absent it exits 127, so EmitPaged falls back to the raw roff rather than
    // showing an empty page. Runs via `sh -c`, so the pipeline / ${PAGER:-...} expand.
    return "if command -v mandoc >/dev/null 2>&1; then mandoc | ${PAGER:-less -FRX}; else exit 127; fi";
  }
  return ResolveTextPager();
}

void EmitPaged(std::string_view text, PagerWhen when, bool stdout_is_tty, PagerKind kind) {
  // kAll is kAuto for the meta surfaces: it only ADDS the listing, it does not change how a
  // help page is paged.
  const bool page =
      when == PagerWhen::kAlways || ((when == PagerWhen::kAuto || when == PagerWhen::kAll) && stdout_is_tty);
  if (!page) {
    WriteToStdout(text);
    return;
  }
  const std::string command = ResolvePagerCommand(kind);
  if (command.empty() || !PipeThroughPager(text, command)) {
    WriteToStdout(text);
  }
}

namespace {

// SIGPIPE is ignored while a streaming pager is attached, so a quit at the first screen turns
// into failing writes we can swallow rather than a signal death mid-walk. The previous
// disposition is restored when the pager is finished.
struct ::sigaction g_previous_sigpipe;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace

PagerStream::PagerStream(PagerWhen when, bool stdout_is_tty, bool suppressed) {
  // Everything that says "no pager here" is one condition: kAll is the only value that pages a
  // listing, a pager needs a terminal to page onto, and the caller can veto outright.
  if (when != PagerWhen::kAll || !stdout_is_tty || suppressed) {
    return;
  }
  const std::string command = ResolvePagerCommand(PagerKind::kText);
  if (command.empty()) {
    return;  // $XFF_PAGER / $PAGER set to empty means "no pager", as for the meta surfaces
  }
  std::array<int, 2> fds{};
  // macOS has no pipe2(), so no CLOEXEC here; the child closes both raw ends before exec.
  if (::pipe(fds.data()) != 0) {  // NOLINT(android-cloexec-pipe)
    return;
  }
  const int read_fd = fds[0];
  const int write_fd = fds[1];
  const ::pid_t pid = ::fork();
  if (pid < 0) {
    ::close(read_fd);
    ::close(write_fd);
    return;  // could not fork: run unpaged rather than not at all
  }
  if (pid == 0) {
    // Child: our stdout becomes its stdin; it draws on the terminal we still share.
    ::dup2(read_fd, STDIN_FILENO);
    ::close(read_fd);
    ::close(write_fd);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ::execlp("sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
    ::_exit(127);
  }
  // Parent: from here on, THIS process's stdout is the pipe, so every writer is paged without
  // being told - std::cout, the renderers, and any child that inherits our descriptors.
  ::close(read_fd);
  std::cout.flush();
  // A plain dup, not F_DUPFD_CLOEXEC: this fd exists to be restored onto STDOUT_FILENO, and any
  // child xff spawns while the pager is attached should inherit the paged stdout, not lose it.
  saved_stdout_ = ::dup(STDOUT_FILENO);  // NOLINT(android-cloexec-dup)
  ::dup2(write_fd, STDOUT_FILENO);
  ::close(write_fd);
  struct ::sigaction ignore{};
  ignore.sa_handler = SIG_IGN;
  ::sigaction(SIGPIPE, &ignore, &g_previous_sigpipe);
  pid_ = pid;
  active_ = true;
}

void PagerStream::Finish() {
  if (!active_) {
    return;
  }
  active_ = false;
  // Flushing before the swap is what actually delivers the tail of the listing. A failed flush
  // (the user quit the pager) is deliberately ignored, and the stream's error state cleared, so
  // anything printed after the restore still reaches the terminal.
  std::cout.flush();
  std::cout.clear();
  ::dup2(saved_stdout_, STDOUT_FILENO);  // also closes the pipe, so the pager sees EOF
  ::close(saved_stdout_);
  saved_stdout_ = -1;
  ::sigaction(SIGPIPE, &g_previous_sigpipe, nullptr);
  int status = 0;
  ::waitpid(pid_, &status, 0);  // blocks until the user quits the pager, as a pager should
  pid_ = -1;
}

PagerStream::~PagerStream() {
  Finish();
}

}  // namespace xff::cli
