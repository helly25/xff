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
#include <string>
#include <string_view>
#include <vector>

#include "xff/vfs/entry.h"

namespace xff::color {

When ResolveWhen(const std::vector<std::string>& globals) {
  When when = When::kAuto;
  for (const std::string& global : globals) {
    if (global == "--color" || global == "--color=always") {
      when = When::kAlways;
    } else if (global == "--color=auto") {
      when = When::kAuto;
    } else if (global == "--color=never") {
      when = When::kNever;
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
    case vfs::FileType::kDirectory: return "1;34";    // bold blue
    case vfs::FileType::kSymlink: return "1;36";      // bold cyan
    case vfs::FileType::kFifo: return "33";           // yellow
    case vfs::FileType::kSocket: return "1;35";       // bold magenta
    case vfs::FileType::kBlockDevice: return "1;33";  // bold yellow
    case vfs::FileType::kCharDevice: return "1;33";   // bold yellow
    case vfs::FileType::kRegular:
      return (mode & 0111U) != 0U ? std::string_view("1;32") : std::string_view();  // bold green if executable
    case vfs::FileType::kUnknown: return {};
  }
  return {};
}

}  // namespace xff::color
