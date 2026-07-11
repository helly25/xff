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

#ifndef XFF_GLOB_GLOB_H_
#define XFF_GLOB_GLOB_H_

#include <string>
#include <string_view>

namespace xff::glob {

// Translates a shell glob `pattern` into an RE2 pattern, matched anchored (via RE2::FullMatch), so
// the `^`/`$` anchors are implicit. It is path-segment aware, the shell / gitignore semantics:
//   `*`         one path segment's worth of non-slash (`[^/]*`)
//   `?`         a single non-slash (`[^/]`)
//   `[...]`     a character class: ranges (`[a-z]`), POSIX classes (`[[:alpha:]]`), a leading `!`
//               negates (`[^...]`), and a leading `]` is a literal member - all handed to RE2
//   `**`        as a WHOLE segment, cross-directory: `**/` (or a leading `**`) is zero or more
//               directories, a trailing `/**` is everything below; a `**` glued to other characters
//               degrades to a single `*`
//   `\x`        an escaped literal `x`
//   otherwise   the character literal (RE2 metacharacters escaped)
//
// This is the shared translator behind the gitignore engine (xff/ignore, which strips gitignore's
// leading `!` / anchoring `/` / trailing `/` first) and the --regextype=GLOB matcher (xff/regex,
// which compiles the result as RE2 so FullMatch/PartialMatch/FindFirst/Rewrite all fall out of RE2).
//
// The `**` rules are gitignore's on purpose (a `**` crosses directories only as a whole segment; a
// glued `**` stays within one segment). That is why this is a small self-contained translator and
// NOT mbo::file::Glob2Re2 - a full filesystem-globbing library with different `**` semantics that xff
// does not use (xff walks its own VFS engine and here needs only the pure pattern -> RE2 step). Brace
// expansion `{a,b}` is deliberately NOT handled here (`{`/`}` stay literal); it will be a separate
// opt-in shell-glob grammar so GLOB / gitignore keep matching literal braces.
std::string GlobToRegex(std::string_view pattern);

}  // namespace xff::glob

#endif  // XFF_GLOB_GLOB_H_
