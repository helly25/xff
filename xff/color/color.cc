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
  Scheme scheme = Scheme::kLs;
  for (const std::string& global : globals) {
    std::string_view value = global;
    if (!absl::ConsumePrefix(&value, "--color-scheme=")) {
      continue;
    }
    if (value == "xff") {
      scheme = Scheme::kXff;
    } else if (value == "ls") {
      scheme = Scheme::kLs;
    }
    // Anything else leaves the prior resolution, matching how --color treats an unknown value.
  }
  return scheme;
}

Palette Palette::FromLsColors(std::string_view ls_colors) {
  Palette palette;
  for (std::string_view entry : absl::StrSplit(ls_colors, ':', absl::SkipEmpty())) {
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

std::string_view Palette::CodeFor(std::string_view name, vfs::FileType type, std::uint32_t mode) const {
  // The dircolors key for this entry's TYPE, when $LS_COLORS named one; the built-in scheme answers
  // for anything it did not.
  const auto typed = [this](std::string_view key) -> const std::string* {
    const auto it = types_.find(key);
    return it == types_.end() ? nullptr : &it->second;
  };
  switch (type) {
    case vfs::FileType::kDirectory:
      if (const std::string* code = typed("di")) {
        return *code;
      }
      break;
    case vfs::FileType::kSymlink:
      if (const std::string* code = typed("ln")) {
        return *code;
      }
      break;
    case vfs::FileType::kFifo:
      if (const std::string* code = typed("pi")) {
        return *code;
      }
      break;
    case vfs::FileType::kSocket:
      if (const std::string* code = typed("so")) {
        return *code;
      }
      break;
    case vfs::FileType::kBlockDevice:
      if (const std::string* code = typed("bd")) {
        return *code;
      }
      break;
    case vfs::FileType::kCharDevice:
      if (const std::string* code = typed("cd")) {
        return *code;
      }
      break;
    case vfs::FileType::kRegular: {
      // ls's order for a regular file: the executable bit first, then the extension, then `fi`. So a
      // themed `*.sh` loses to `ex` on an executable script, exactly as in a real ls listing.
      if ((mode & 0111U) != 0U) {
        if (const std::string* code = typed("ex")) {
          return *code;
        }
        break;
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
      if (const std::string* code = typed("fi")) {
        return *code;
      }
      break;
    }
    case vfs::FileType::kUnknown: break;
  }
  return CodeForType(type, mode);
}

}  // namespace xff::color
