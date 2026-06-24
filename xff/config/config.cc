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

#include "xff/config/config.h"

#include <string>
#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/str_cat.h"
#include "xff/config/ini.h"
#include "xff/config/xffrc.h"

namespace xff::config {
namespace {

// An .xffrc line applies under the active --config selectors when its base is
// "common"/empty or names an active config, AND its config is empty or names one.
bool LineApplies(const RcLine& line, const std::vector<std::string>& configs) {
  const bool base_ok = line.base.empty() || line.base == "common" || absl::c_contains(configs, line.base);
  const bool config_ok = line.config.empty() || absl::c_contains(configs, line.config);
  return base_ok && config_ok;
}

void AppendMatching(
    std::vector<ResolvedFlag>& out,
    const std::vector<RcLine>& lines,
    const std::vector<std::string>& configs,
    Source source) {
  for (const RcLine& line : lines) {
    if (!LineApplies(line, configs)) {
      continue;
    }
    for (const std::string& flag : line.flags) {
      out.push_back(ResolvedFlag{.flag = flag, .source = source});
    }
  }
}

}  // namespace

std::vector<ResolvedFlag> ResolveConfig(const ConfigInputs& inputs) {
  std::vector<ResolvedFlag> resolved;
  if (inputs.no_config) {
    return resolved;  // pure CLI + built-ins; the system policy (read elsewhere) still bounds the run
  }
  for (const std::string& flag : inputs.system.defaults) {  // global system defaults, lowest precedence
    resolved.push_back(ResolvedFlag{.flag = flag, .source = Source::kSystem});
  }
  AppendMatching(resolved, inputs.user, inputs.configs, Source::kUser);
  AppendMatching(resolved, inputs.project, inputs.configs, Source::kProject);
  return resolved;
}

std::string_view SourceName(Source source) {
  switch (source) {
    case Source::kCli: return "cli";
    case Source::kProject: return "project";
    case Source::kSystem: return "system";
    case Source::kUnset: return "unset";
    case Source::kUser: return "user";
  }
  return "unset";
}

registry::Style ActiveStyle(const std::vector<std::string>& configs) {
  registry::Style style = registry::Style::kXff;  // the modern xff style is the default
  for (const std::string& name : configs) {
    std::string_view base = name;
    base = base.substr(0, base.find(':'));  // "xff:2" pins an epoch; the base picks the style
    if (base == "find") {
      style = registry::Style::kFind;
    } else if (base == "xff") {
      style = registry::Style::kXff;
    }
  }
  return style;
}

std::string_view DefaultStyleForProgram(std::string_view argv0) {
  if (const std::string_view::size_type slash = argv0.rfind('/'); slash != std::string_view::npos) {
    argv0 = argv0.substr(slash + 1);  // basename: the last path component
  }
  return argv0 == "find" ? "find" : "xff";
}

std::string ExplainConfig(const std::vector<ResolvedFlag>& resolved, const std::vector<std::string>& cli_globals) {
  std::string out = "# xff effective configuration (application order; later overrides earlier)\n";
  for (const ResolvedFlag& flag : resolved) {
    absl::StrAppend(&out, SourceName(flag.source), "\t", flag.flag, "\n");
  }
  for (const std::string& flag : cli_globals) {
    absl::StrAppend(&out, "cli\t", flag, "\n");
  }
  return out;
}

std::string ExplainSources(const std::vector<ConfigSource>& sources, registry::Style style) {
  std::string out = absl::StrCat("# xff active style: ", style == registry::Style::kFind ? "find" : "xff", "\n");
  absl::StrAppend(&out, "# config sources consulted (precedence order)\n");
  for (const ConfigSource& source : sources) {
    absl::StrAppend(
        &out, "source\t", SourceName(source.layer), "\t", source.found ? "found" : "absent", "\t", source.path, "\n");
  }
  return out;
}

}  // namespace xff::config
