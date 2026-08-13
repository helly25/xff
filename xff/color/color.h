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

#include "absl/container/flat_hash_map.h"
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
//
// xff's BUILT-IN scheme. `Palette` below is what a run actually consults, because the colours a user
// expects are usually the ones their `ls` already uses.
std::string_view CodeForType(vfs::FileType type, std::uint32_t mode);

// Which palette a run colours with (`--color-scheme`). Four distinct answers, because "use ls's
// colours" turns out to mean three different things and each deserves its own name:
enum class Scheme : std::uint8_t {
  kAuto,      // ls OR xff: the theme when $LS_COLORS is set at all, else xff's scheme - the DEFAULT
  kLs,        // $LS_COLORS ALONE: what it does not name prints uncoloured, as in a real ls listing
  kLsAndXff,  // ls AND xff: the theme where it speaks, xff's colour for every key it omits
  kXff,       // xff's built-in scheme, ignoring $LS_COLORS
};

// Resolves `--color-scheme=auto|ls|ls-and-xff|xff` (last occurrence wins); an absent or unrecognised
// value leaves the default, kAuto, so a themed terminal is honoured without asking and a machine with
// no theme still gets colour.
//
// The spellings follow logic's own algebra, where `+` is OR and the merge is AND:
//   auto == ls+xff == ls-or-xff == default   the theme OR xff's scheme, decided per VARIABLE
//   ls-and-xff                               the theme AND xff's scheme, merged per KEY
// (`ls&xff` is deliberately NOT accepted: an unquoted `&` backgrounds the command in every shell, and
// a spelling that only works quoted is a trap, the same reason `-z*` was rejected for the archive
// umbrella.) `default` is there so a config file can name the default without hard-coding which
// scheme that currently is.
Scheme ResolveScheme(const std::vector<std::string>& globals);

// The palette `scheme` describes, given the raw `$LS_COLORS` value (empty when unset). The one place
// the four schemes turn into a lookup, so a caller never has to know which of them falls back how.
class Palette;
[[nodiscard]] Palette PaletteFor(Scheme scheme, std::string_view ls_colors);

// The colours one run uses. Built once (colours are a whole-run choice, not a per-entry one) and
// consulted by every colourised surface, so the plain listing and `-ls` cannot disagree.
//
// A palette is more than a type -> code table because `$LS_COLORS` colours by NAME as well: its
// `*.tar=01;31` entries are per-extension, which xff's own scheme has no equivalent for. Hence
// `CodeFor` takes the name, and the lookup follows `ls`: a non-regular file by its type, a regular
// one by executable-bit first and then by extension.
class Palette {
 public:
  // xff's built-in scheme only (`--color-scheme=xff`, or kLs with no $LS_COLORS set).
  Palette() = default;

  // `ls_colors` (the raw `$LS_COLORS` value, `key=value:key=value:...`) as the palette, with or
  // without xff's scheme behind it: `fall_back` = true is kLsXff (an omitted key keeps xff's colour),
  // false is kLs (an omitted key means uncoloured, as in a real ls listing). Unparsable entries are
  // skipped rather than failing the run: a malformed variable is not worth refusing to list files
  // over, and `ls` itself ignores them.
  static Palette FromLsColors(std::string_view ls_colors, bool fall_back = true);

  // The SGR parameter for one entry, or empty for "print it uncoloured". `mode` is the raw st_mode.
  [[nodiscard]] std::string_view CodeFor(std::string_view name, vfs::FileType type, std::uint32_t mode) const;

 private:
  // The theme's code for a two-letter dircolors key, or null when it named none. Null and EMPTY are
  // different answers: empty is the theme saying "leave these plain".
  [[nodiscard]] const std::string* Themed(std::string_view key) const;

  // A regular file's colour, which is the only case with an order rather than a single key: the
  // executable bit (`ex`), then the extension table, then `fi`. Split out so CodeFor stays a
  // type-to-key lookup.
  [[nodiscard]] std::string_view RegularCode(std::string_view name, std::uint32_t mode) const;

  // The two-letter dircolors type keys this honours, resolved at parse time so the lookup is a
  // single hash probe. Empty (not absent) means "$LS_COLORS says: no colour for this".
  absl::flat_hash_map<std::string, std::string> types_;
  // `*.ext` entries, keyed by the LOWERCASED extension including its dot, since a themed terminal
  // should colour `A.TAR` like `a.tar`.
  absl::flat_hash_map<std::string, std::string> extensions_;
  // Whether a key `$LS_COLORS` did not name keeps xff's built-in colour (kLsAndXff) or is left
  // uncoloured (kLs). A default-constructed palette IS the built-in scheme, so this is true there
  // too, and every lookup ends in CodeForType.
  bool fall_back_ = true;
};

}  // namespace xff::color

#endif  // XFF_COLOR_COLOR_H_
