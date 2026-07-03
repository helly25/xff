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

#include "xff/repo/repo.h"

#include <optional>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "xff/vfs/filesystem.h"

namespace xff::repo {
namespace {

// Expands a leading `~` / `~/` in a git config path value to `home` (git's behavior);
// leaves any other value untouched. Empty `home` leaves a `~` value unexpanded.
std::string ExpandTilde(std::string_view value, std::string_view home) {
  if (home.empty()) {
    return std::string(value);
  }
  if (value == "~") {
    return std::string(home);
  }
  if (value.starts_with("~/")) {
    return absl::StrCat(home, value.substr(1));  // keep the '/'
  }
  return std::string(value);
}

// Scans git config `text` (minimal INI) for `core.excludesfile`, returning the last
// value (later lines win, like git). Only the `[core]` section is considered; keys are
// case-insensitive; surrounding double quotes on the value are stripped.
std::optional<std::string> ParseExcludesFile(std::string_view text) {
  bool in_core = false;
  std::optional<std::string> result;
  for (std::string_view line : absl::StrSplit(text, '\n')) {
    line = absl::StripAsciiWhitespace(line);
    if (line.empty() || line.front() == '#' || line.front() == ';') {
      continue;
    }
    if (line.front() == '[') {
      const std::string_view::size_type close = line.find(']');
      const std::string_view name =
          close == std::string_view::npos ? std::string_view() : absl::StripAsciiWhitespace(line.substr(1, close - 1));
      in_core = absl::EqualsIgnoreCase(name, "core");  // core has no subsection
      continue;
    }
    if (!in_core) {
      continue;
    }
    const std::string_view::size_type eq = line.find('=');
    if (eq == std::string_view::npos) {
      continue;
    }
    if (!absl::EqualsIgnoreCase(absl::StripAsciiWhitespace(line.substr(0, eq)), "excludesfile")) {
      continue;
    }
    std::string_view value = absl::StripAsciiWhitespace(line.substr(eq + 1));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      value = value.substr(1, value.size() - 2);
    }
    result = std::string(value);
  }
  return result;
}

}  // namespace

std::optional<std::string> FindRepoRoot(const vfs::FileSystem& fs, std::string_view start_dir) {
  std::string dir(start_dir);
  while (!dir.empty()) {
    // Probe `<dir>/.git` (avoid a "//" when dir is the root "/"). Stat succeeds for
    // both a `.git` directory and a `.git` file, which is exactly what we want.
    const std::string dot_git = dir == "/" ? "/.git" : absl::StrCat(dir, "/.git");
    if (fs.Stat(dot_git, /*follow_symlinks=*/false).ok()) {
      return dir;
    }
    if (dir == "/") {
      break;  // reached the filesystem root without a hit
    }
    const std::string::size_type slash = dir.rfind('/');
    // Parent of "/foo" is "/"; parent of "/a/b" is "/a". A path with no slash is
    // not absolute (the caller's contract), so stop rather than loop forever.
    dir = slash == std::string::npos ? std::string() : (slash == 0 ? std::string("/") : dir.substr(0, slash));
  }
  return std::nullopt;
}

std::optional<std::string> GlobalExcludesPath(const vfs::FileSystem& fs, const GitConfigEnv& env) {
  // git's config home: $XDG_CONFIG_HOME, else $HOME/.config.
  const std::string config_home = !env.xdg_config_home.empty() ? env.xdg_config_home
                                  : !env.home.empty()          ? absl::StrCat(env.home, "/.config")
                                                               : std::string();
  // Read core.excludesFile from $XDG_CONFIG_HOME/git/config then ~/.gitconfig; the
  // latter (per-user) wins over the former (XDG), matching git's precedence.
  std::optional<std::string> excludes;
  if (!config_home.empty()) {
    if (const absl::StatusOr<std::string> text = fs.ReadContent(absl::StrCat(config_home, "/git/config")); text.ok()) {
      if (std::optional<std::string> value = ParseExcludesFile(*text)) {
        excludes = std::move(value);
      }
    }
  }
  if (!env.home.empty()) {
    if (const absl::StatusOr<std::string> text = fs.ReadContent(absl::StrCat(env.home, "/.gitconfig")); text.ok()) {
      if (std::optional<std::string> value = ParseExcludesFile(*text)) {
        excludes = std::move(value);
      }
    }
  }
  if (excludes.has_value()) {
    return ExpandTilde(*excludes, env.home);
  }
  // Unset: git's default global ignore is $XDG_CONFIG_HOME/git/ignore (~/.config/git/ignore).
  if (!config_home.empty()) {
    return absl::StrCat(config_home, "/git/ignore");
  }
  return std::nullopt;
}

}  // namespace xff::repo
