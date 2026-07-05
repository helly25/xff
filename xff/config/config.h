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

// How a per-directory (project) .xffrc is treated (--project-config). A project file lives in a
// tree the user may not control, so it is off unless explicitly enabled: kOn applies it (still
// safe-subset only -- GateConfig drops sensitive/destructive project lines regardless, and style
// selectors are never taken from a project file); kWarn (the default) ignores it but lets the CLI
// note that one was found; kOff ignores it silently. Full config lives in user/system files.
enum class ProjectConfigMode { kOff, kWarn, kOn };

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

// Whether `name` is one of the built-in preset styles (find / xff / rg / xfd). Tests the base of
// a selector (so "xff:2" should be reduced to "xff" first). Those names are reserved: a config
// file may not attach behavior to a preset (see GateConfig's preset-overload rule), and argv[0]
// dispatch uses this to tell a preset invocation name from a custom alias (which selects a named
// config instead).
bool IsBuiltinStyle(std::string_view name);

// The active find/xff style selected by the --config stack. A --config=NAME whose
// base (the part before any ':') is "find" or "xff" picks that style; selectors
// stack, so the last style selector wins. With no style selector the default is
// the modern xff style. Custom config names (e.g. "debug") and version-pinned
// epochs ("xff:2" -> base "xff") leave the mapping unchanged. The strict find
// style is what makes a `find`-style run reject xff-only primaries (see
// parser::EnforceStyle); design-config.md "CLI selectors".
registry::Style ActiveStyle(const std::vector<std::string>& configs);

// The leading --config selector implied by the program name (argv[0] dispatch), from its
// basename: a built-in style name ("find"/"xff"/"rg"/"xfd") selects that preset ("fd" is the
// fd-like alias -> "xfd"); an empty name defaults to "xff"; ANY OTHER name is returned verbatim as
// a NAMED-config selector (e.g. a "mytool" symlink -> "mytool"), which activates a matching
// `mytool:` config block while leaving the base style at the modern xff default (ActiveStyle
// ignores a non-style selector). main() prepends this as the lowest-precedence selector, so an
// explicit --config still stacks over it via ActiveStyle's last-wins (design-config.md "CLI
// selectors"). The returned view aliases `argv0` for a passthrough name; copy it to retain.
std::string_view DefaultStyleForProgram(std::string_view argv0);

// The project-config mode from the CLI globals: the last --project-config=on|warn|off wins, and
// the default (no flag, or an unrecognized value) is kWarn. main() uses it to decide whether a
// discovered per-directory .xffrc is applied (kOn), ignored with a stderr note (kWarn), or ignored
// silently (kOff). Only the project layer is affected; user/system config is always applied.
ProjectConfigMode ResolveProjectConfigMode(const std::vector<std::string>& globals);

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
