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

#ifndef XFF_CONFIG_CONFIG_H_
#define XFF_CONFIG_CONFIG_H_

#include <string>
#include <string_view>
#include <vector>

#include "xff/config/ini.h"
#include "xff/config/xffrc.h"
#include "xff/registry/descriptor.h"

namespace xff::config {

// Provenance of a resolved setting: which layer contributed it. Resolution is
// last-non-unset-wins; kUnset is the "no override" sentinel and is never stored.
enum class Source { kUnset, kSystem, kUser, kProject, kCli };

// One resolved config flag plus the layer it came from.
struct ResolvedFlag {
  std::string flag;
  Source source;
};

// One config file consulted during discovery, recorded for --explain's source
// trace: its path, the layer it would feed, and whether it existed/was readable.
struct ConfigSource {
  std::string path;
  Source layer;
  bool found = false;
};

// The parsed layers + active selectors fed to ResolveConfig. CLI flags are NOT
// here: the caller applies them last (highest precedence) after this resolution.
struct ConfigInputs {
  SystemConfig system;                // parsed /etc/xff.ini ([defaults] + [policy])
  std::vector<RcLine> user;           // parsed user .xffrc
  std::vector<RcLine> project;        // parsed project .xffrc (single file for now; cascade is phase E)
  std::vector<std::string> configs;   // active --config=NAME selectors (styles and/or named configs)
  bool no_config = false;             // --no-config: drop user+project layers and system defaults
  std::vector<ConfigSource> sources;  // every file consulted during discovery, for --explain (set by Discover)
};

// Resolves config-supplied flags, lowest precedence first (system [defaults] <
// user .xffrc < project .xffrc), each tagged with its Source; the caller appends
// CLI flags afterwards (they win). An .xffrc line contributes its flags when its
// base selector is empty/"common" or names an active --config, AND its config
// selector is empty or names an active --config. --no-config yields an empty
// result (pure CLI + built-ins); the system *policy* is never dropped (it is read
// elsewhere and bounds the run regardless). No capability gating yet (phase C).
std::vector<ResolvedFlag> ResolveConfig(const ConfigInputs& inputs);

// The lowercase layer name for a Source: "unset"/"system"/"user"/"project"/"cli".
std::string_view SourceName(Source source);

// The active find/xff style selected by the --config stack. A --config=NAME whose
// base (the part before any ':') is "find" or "xff" picks that style; selectors
// stack, so the last style selector wins. With no style selector the default is
// the modern xff style. Custom config names (e.g. "debug") and version-pinned
// epochs ("xff:2" -> base "xff") leave the mapping unchanged. The strict find
// style is what makes a `find`-style run reject xff-only primaries (see
// parser::EnforceStyle); design-config.md "CLI selectors".
registry::Style ActiveStyle(const std::vector<std::string>& configs);

// The default --config style selected by the program name (argv[0] dispatch): a
// basename of "find" selects the strict find style, anything else (the canonical
// "xff", or any other alias) selects the modern xff style. main() prepends this
// as the lowest-precedence selector, so an explicit --config still overrides it
// via ActiveStyle's last-wins (design-config.md "CLI selectors"). Returns the
// selector string "find" or "xff".
std::string_view DefaultStyleForProgram(std::string_view argv0);

// Renders the effective configuration for --explain: the resolved config flags
// (each prefixed by its provenance) in application order, then the CLI globals
// (provenance "cli"). Later lines override earlier ones, mirroring resolution.
std::string ExplainConfig(const std::vector<ResolvedFlag>& resolved, const std::vector<std::string>& cli_globals);

// Renders the discovery trace for --explain: the active find/xff style, then every
// config file consulted (precedence order: system < user < project), each tagged
// with its layer and whether it was found. Pairs with ExplainConfig (which shows
// what each contributed); together they answer "what did xff read, and why".
std::string ExplainSources(const std::vector<ConfigSource>& sources, registry::Style style);

}  // namespace xff::config

#endif  // XFF_CONFIG_CONFIG_H_
