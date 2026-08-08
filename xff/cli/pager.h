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

#ifndef XFF_CLI_PAGER_H_
#define XFF_CLI_PAGER_H_

#include <string>
#include <string_view>
#include <vector>

namespace xff::cli {

// --pager=auto|always|never: whether to page the long meta / doc output (--help,
// --help=TOPIC, --man, --markdown). kAuto (the default) pages only on a terminal;
// this mirrors --color's tri-state so the two read the same. It never affects the
// file listing - that is what a shell pipe to a pager is for.
enum class PagerWhen { kAuto, kAlways, kNever };

// Resolves the --pager=WHEN globals from the raw args (scanned before the parse, like
// --color / --width): last occurrence wins; bare --pager == =always; --no-pager ==
// =never; an absent or unrecognised value is kAuto.
PagerWhen ResolvePagerWhen(const std::vector<std::string>& args);

// The pager command line: $XFF_PAGER, else $PAGER, else the built-in "less -FRX"
// (-F quits if it fits one screen, -R keeps our ANSI color, -X leaves short output on
// the normal screen). An environment variable that is set but empty means "no pager"
// and yields "" - EmitPaged then writes straight to stdout (the git / aws convention).
std::string ResolvePagerCommand();

// Writes `text` to stdout, paging it when `when` + `stdout_is_tty` call for a pager
// and a command is available; otherwise straight to std::cout. Paging runs the command
// through `sh -c` (so a $PAGER with args or a pipeline works) with `text` on its stdin.
// A missing TTY, an empty command, or a fork / pipe failure falls back to std::cout so
// the output is never lost.
void EmitPaged(std::string_view text, PagerWhen when, bool stdout_is_tty);

}  // namespace xff::cli

#endif  // XFF_CLI_PAGER_H_
