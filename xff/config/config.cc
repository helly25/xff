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

#include "xff/config/ini.h"
#include "xff/config/xffrc.h"

namespace xff::config {
namespace {

bool Contains(const std::vector<std::string>& haystack, std::string_view needle) {
  for (const std::string& item : haystack) {
    if (item == needle) {
      return true;
    }
  }
  return false;
}

// An .xffrc line applies under the active --config selectors when its base is
// "common"/empty or names an active config, AND its config is empty or names one.
bool LineApplies(const RcLine& line, const std::vector<std::string>& configs) {
  const bool base_ok = line.base.empty() || line.base == "common" || Contains(configs, line.base);
  const bool config_ok = line.config.empty() || Contains(configs, line.config);
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

}  // namespace xff::config
