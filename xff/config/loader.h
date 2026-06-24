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

#ifndef XFF_CONFIG_LOADER_H_
#define XFF_CONFIG_LOADER_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/functional/function_ref.h"
#include "xff/config/config.h"

namespace xff::config {

// Reads the file at `path`, returning its contents, or nullopt if the file is
// missing or unreadable. Injected so discovery is testable without touching the
// real filesystem; run.cc supplies a std::ifstream-backed reader.
using FileReader = absl::FunctionRef<std::optional<std::string>(std::string_view path)>;

// Inputs to Discover: the CLI selectors plus the environment values that locate
// the user config (injected rather than read from getenv here, for testability).
struct DiscoveryOptions {
  bool no_config = false;                      // --no-config
  std::vector<std::string> configs;            // --config=NAME, in order
  std::vector<std::string> xffrc_files;        // --xffrc=FILE, in order
  std::vector<std::string> roots;              // absolute search-root dirs; the project .xffrc cascade walks each
  std::optional<std::string> xff_config;       // $XFF_CONFIG
  std::optional<std::string> xdg_config_home;  // $XDG_CONFIG_HOME
  std::optional<std::string> home;             // $HOME
};

// The user config path per the discovery order: $XFF_CONFIG, else
// $XDG_CONFIG_HOME/xff/config, else $HOME/.config/xff/config. Empty if none of
// those is set.
std::string UserConfigPath(const DiscoveryOptions& opts);

// Discovers and parses the config layers into ConfigInputs (ready for
// ResolveConfig), reading every file through `read`:
//   - system: /etc/xff.ini (always read; its [policy] is never skipped),
//   - user:   UserConfigPath(opts), in the .xffrc grammar,
//   - --xffrc=FILE: appended to the user layer (naming the file is the consent),
//   - project: the .xffrc cascade -- for each absolute dir in opts.roots, every
//     .xffrc from the filesystem root down to that dir, shallowest first (deeper
//     overrides), deduped across roots (untrusted; the policy gate makes it safe).
// --no-config skips the user + project layers and the explicit files (ResolveConfig
// then also drops the system [defaults]); the system file is still read so its
// policy is available to the gate (phase C). Per-entry subtree scoping for files
// below a root (config varying mid-walk) is deferred; see TODO.md.
ConfigInputs Discover(const DiscoveryOptions& opts, FileReader read);

// Extracts the config selectors among `globals` into a DiscoveryOptions (the env
// fields are left unset for the caller): --no-config, --config=NAME (in order),
// and --xffrc=FILE (in order). Every other global is ignored.
DiscoveryOptions SelectorsFromGlobals(const std::vector<std::string>& globals);

}  // namespace xff::config

#endif  // XFF_CONFIG_LOADER_H_
