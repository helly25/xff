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

#include "xff/color/color.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "mbo/container/limited_map.h"
#include "xff/values/values.h"
#include "xff/vfs/entry.h"

namespace xff::color {

When ResolveWhen(const std::vector<std::string>& globals) {
  When when = When::kAuto;
  for (const std::string& global : globals) {
    std::string_view value = global;
    if (value == "--color") {
      when = When::kAlways;  // a bare --color forces color on
    } else if (absl::ConsumePrefix(&value, "--color=")) {
      // The shared vocabulary (auto / always / never plus yes / no / 1 / true / 0 /
      // false); an unrecognized value is ignored, leaving the prior resolution.
      if (const std::optional<values::Tristate> tri = values::ParseTristate(value); tri.has_value()) {
        switch (*tri) {
          case values::Tristate::kAuto: when = When::kAuto; break;
          case values::Tristate::kOff: when = When::kNever; break;
          case values::Tristate::kOn: when = When::kAlways; break;
        }
      }
    }
  }
  return when;
}

bool Enabled(When when, bool stdout_is_tty, bool no_color_env) {
  switch (when) {
    case When::kAlways: return true;  // an explicit request overrides NO_COLOR
    case When::kNever: return false;
    case When::kAuto: return stdout_is_tty && !no_color_env;
  }
  return false;
}

std::string_view CodeForType(vfs::FileType type, std::uint32_t mode) {
  switch (type) {
    case vfs::FileType::kDirectory: return "1;34";  // bold blue
    case vfs::FileType::kSymlink: return "1;36";    // bold cyan
    case vfs::FileType::kFifo: return "33";         // yellow
    case vfs::FileType::kSocket: return "1;35";     // bold magenta
    case vfs::FileType::kBlockDevice:
    case vfs::FileType::kCharDevice: return "1;33";  // bold yellow (block / char device)
    case vfs::FileType::kRegular:
      return (mode & 0111U) != 0U ? std::string_view("1;32") : std::string_view();  // bold green if executable
    case vfs::FileType::kUnknown: return {};
  }
  return {};
}

Scheme ResolveScheme(const std::vector<std::string>& globals) {
  // The accepted spellings, following logic's algebra: `+` is OR (the per-variable default), and the
  // per-key merge is AND. `default` is a synonym for whatever the default is, for a config file.
  // `ls&xff` is deliberately absent: an unquoted `&` backgrounds the command.
  static constexpr auto kSchemes = mbo::container::MakeLimitedMap(
      std::pair{std::string_view("auto"), Scheme::kAuto}, std::pair{std::string_view("default"), Scheme::kAuto},
      std::pair{std::string_view("ls"), Scheme::kLs}, std::pair{std::string_view("ls+xff"), Scheme::kAuto},
      std::pair{std::string_view("ls-and-xff"), Scheme::kLsAndXff},
      std::pair{std::string_view("ls-or-xff"), Scheme::kAuto}, std::pair{std::string_view("xff"), Scheme::kXff});
  Scheme scheme = Scheme::kAuto;
  for (const std::string& global : globals) {
    std::string_view value = global;
    if (!absl::ConsumePrefix(&value, "--color-scheme=")) {
      continue;
    }
    // Anything unrecognised leaves the prior resolution, matching how --color treats a bad value.
    if (const auto it = kSchemes.find(value); it != kSchemes.end()) {
      scheme = it->second;
    }
  }
  return scheme;
}

Palette PaletteFor(Scheme scheme, std::string_view ls_colors) {
  switch (scheme) {
    case Scheme::kXff: return {};
    case Scheme::kLs: return Palette::FromLsColors(ls_colors, /*fall_back=*/false);
    case Scheme::kLsAndXff: return Palette::FromLsColors(ls_colors, /*fall_back=*/true);
    case Scheme::kAuto:
      // Per-VARIABLE rather than per-key: a set theme is taken as the whole answer, an unset one
      // leaves xff's scheme untouched.
      return ls_colors.empty() ? Palette() : Palette::FromLsColors(ls_colors, /*fall_back=*/false);
  }
  return {};
}

Palette Palette::FromLsColors(std::string_view ls_colors, bool fall_back) {
  Palette palette;
  palette.fall_back_ = fall_back;
  for (const std::string_view entry : absl::StrSplit(ls_colors, ':', absl::SkipEmpty())) {
    const std::string_view::size_type equals = entry.find('=');
    if (equals == std::string_view::npos) {
      continue;  // not a key=value pair; ls ignores it too
    }
    const std::string_view key = entry.substr(0, equals);
    const std::string_view code = entry.substr(equals + 1);
    if (key.size() > 1 && key.front() == '*') {
      // `*.tar=01;31`: keyed by the lowercased suffix (including the dot), so a themed terminal
      // colours `A.TAR` the way it colours `a.tar`.
      palette.extensions_.insert_or_assign(absl::AsciiStrToLower(key.substr(1)), std::string(code));
      continue;
    }
    if (key.size() == 2) {
      palette.types_.insert_or_assign(std::string(key), std::string(code));
    }
    // Everything else (`rs`, `lc`, `rc`, `ec`, `no`, multi-letter keys) is deliberately unread: xff
    // emits its own reset and never uses the surrounding-code customisation.
  }
  return palette;
}

const std::string* Palette::Themed(std::string_view key) const {
  const auto it = types_.find(key);
  return it == types_.end() ? nullptr : &it->second;
}

std::string_view Palette::RegularCode(std::string_view name, std::uint32_t mode) const {
  // ls's order for a regular file: the executable bit first, then the extension, then `fi`. So a
  // themed `*.sh` loses to `ex` on an executable script, exactly as in a real ls listing.
  if ((mode & 0111U) != 0U) {
    const std::string* const executable = Themed("ex");
    return executable != nullptr ? std::string_view(*executable)
                                 : (fall_back_ ? CodeForType(vfs::FileType::kRegular, mode) : std::string_view());
  }
  if (!extensions_.empty()) {
    const std::string_view::size_type dot = name.rfind('.');
    if (dot != std::string_view::npos && dot + 1 < name.size()) {
      const auto it = extensions_.find(absl::AsciiStrToLower(name.substr(dot)));
      if (it != extensions_.end()) {
        return it->second;
      }
    }
  }
  const std::string* const plain = Themed("fi");
  return plain != nullptr ? std::string_view(*plain)
                          : (fall_back_ ? CodeForType(vfs::FileType::kRegular, mode) : std::string_view());
}

std::string_view Palette::CodeFor(std::string_view name, vfs::FileType type, std::uint32_t mode) const {
  if (type == vfs::FileType::kRegular) {
    return RegularCode(name, mode);
  }
  // Every other type is one dircolors key, so the lookup is a table rather than a branch per case.
  static constexpr auto kKeys = mbo::container::MakeLimitedMap(
      std::pair{vfs::FileType::kBlockDevice, std::string_view("bd")},
      std::pair{vfs::FileType::kCharDevice, std::string_view("cd")},
      std::pair{vfs::FileType::kDirectory, std::string_view("di")},
      std::pair{vfs::FileType::kFifo, std::string_view("pi")},
      std::pair{vfs::FileType::kSocket, std::string_view("so")},
      std::pair{vfs::FileType::kSymlink, std::string_view("ln")});
  if (const auto key = kKeys.find(type); key != kKeys.end()) {
    if (const std::string* const themed = Themed(key->second)) {
      return *themed;
    }
  }
  // Nothing in the theme answered. kLsAndXff keeps xff's colour here; kLs leaves it uncoloured,
  // which is what a real ls does with a type its $LS_COLORS never mentions.
  return fall_back_ ? CodeForType(type, mode) : std::string_view();
}

}  // namespace xff::color
