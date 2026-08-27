// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
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

#ifndef XFF_CLI_PAGER_H_
#define XFF_CLI_PAGER_H_

#include <sys/types.h>

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"

namespace xff::cli {

// --pager=help|auto|always|never|COMMAND. Help (the conservative default) pages only meta
// documents on a terminal; auto also includes listings on a terminal; always and an explicit
// COMMAND are unconditional; never disables paging.
enum class PagerWhen { kHelp, kAuto, kAlways, kNever };

struct PagerConfig {
  PagerWhen when = PagerWhen::kHelp;
  std::string command;  // empty selects the automatic command
};

enum class PagerOutput { kMeta, kListing };
enum class PagerAction { kDirect, kPage };

struct PagerDecision {
  PagerAction action = PagerAction::kDirect;
  std::string command;
};

// The kind of meta output being paged, which picks the default pager. kText is the
// already-terminal-ready surfaces (--help / --markdown / ...). kMan is --man, whose roff
// SOURCE needs formatting first, so its default runs it through a roff formatter.
enum class PagerKind { kText, kMan };

// Resolves the parser-classified pager globals before meta dispatch.
// Last occurrence wins; bare --pager == always; any non-reserved value is an explicit command.
// The removed `all` value is an error rather than an attempted executable.
absl::StatusOr<PagerConfig> ResolvePager(const std::vector<std::string>& args);

// Decides paging policy before an output actor runs. `suppressed` covers output whose expression
// needs direct terminal access or is quiet; it is meaningful primarily for listings.
PagerDecision DecidePager(const PagerConfig& pager, PagerOutput output, bool stdout_is_tty, bool suppressed = false);

// The pager command line for `kind`. kText automatically selects an installed `less -FRX`
// (-F quits if it fits one screen, -R keeps ANSI color, -X leaves short output visible), then
// `more`, $XFF_PAGER, $PAGER, and finally `cat`. An explicit command bypasses that selection.
// kMan: $XFF_MANPAGER wins (and may be a pipeline); otherwise the built-in formats roff with
// `mandoc` and feeds the result through the same text-pager selection. When mandoc is absent the
// built-in exits non-zero so EmitPaged falls back to raw roff. An empty $XFF_MANPAGER disables
// man paging.
std::string ResolvePagerCommand(PagerKind kind = PagerKind::kText, std::string_view explicit_command = {});

// Acts on a completed decision: pages `text` when requested, otherwise writes it directly to
// stdout. `kind` selects the
// default pager (kMan formats roff, see ResolvePagerCommand). Paging runs the command
// through `sh -c` (so a $PAGER with args or a pipeline works) with `text` on its stdin.
// A disabled pager or a fork / pipe failure falls back to std::cout so
// the output is never lost.
void EmitPaged(std::string_view text, const PagerDecision& decision, PagerKind kind = PagerKind::kText);

// Pages the FILE LISTING, which unlike the meta surfaces is produced incrementally over a
// whole walk - so it cannot be buffered into an EmitPaged call. Instead the pager is started
// once and this process's stdout is redirected into it for the object's lifetime, which means
// every writer (std::cout, a renderer, a child process xff spawns) lands in the pager without
// knowing about it, and the first screen appears while the walk is still running.
//
// The caller passes the result of DecidePager, so this actor contains no scope or TTY policy.
//
// Quitting the pager early closes the pipe. SIGPIPE is ignored for the lifetime of the object
// and the resulting write errors are swallowed, so quitting `less` at the first screen ends the
// run quietly instead of printing an I/O error - the same contract EmitPaged has.
class PagerStream {
 public:
  explicit PagerStream(const PagerDecision& decision);
  ~PagerStream();

  // Neither copyable nor movable: it owns a process, a pipe and the process-wide stdout.
  PagerStream(const PagerStream&) = delete;
  PagerStream& operator=(const PagerStream&) = delete;
  PagerStream(PagerStream&&) = delete;
  PagerStream& operator=(PagerStream&&) = delete;

  // Whether a pager was actually started (false means stdout is untouched).
  [[nodiscard]] bool Active() const { return active_; }

  // Restores stdout and waits for the pager, which is what blocks until the user quits it.
  // The destructor calls this; it is idempotent, so a caller that wants the wait at a
  // specific point (before printing to stderr, say) can call it early.
  void Finish();

 private:
  bool active_ = false;
  int saved_stdout_ = -1;
  ::pid_t pid_ = -1;
};

}  // namespace xff::cli

#endif  // XFF_CLI_PAGER_H_
