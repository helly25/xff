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

#ifndef XFF_COLOR_COLOR_H_
#define XFF_COLOR_COLOR_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "xff/vfs/entry.h"

namespace xff::color {

// --color=auto|always|never: when to colorize the plain listing. kAuto (the
// default) colors only when the output is a terminal.
enum class When { kAuto, kAlways, kNever };

// Resolves the --color=WHEN globals: last occurrence wins; bare --color == =always
// (grep/ls); an absent or unrecognised value is kAuto.
When ResolveWhen(const std::vector<std::string>& globals);

// Whether to emit color, combining the mode with the environment: kAlways always,
// kNever never, kAuto only on a terminal with NO_COLOR unset (https://no-color.org).
// --color=always deliberately overrides NO_COLOR (an explicit request wins).
bool Enabled(When when, bool stdout_is_tty, bool no_color_env);

// The ANSI SGR parameter for a file of `type` (and `mode`, for the executable bit),
// ls/fd-style: directory bold blue, symlink bold cyan, executable bold green, and so
// on. Empty for a plain regular file (rendered without color). `mode` is the raw
// st_mode; only the permission bits are consulted.
std::string_view CodeForType(vfs::FileType type, std::uint32_t mode);

}  // namespace xff::color

#endif  // XFF_COLOR_COLOR_H_
