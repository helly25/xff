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

#include "xff/config/policy.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "xff/config/config.h"
#include "xff/config/ini.h"
#include "xff/config/xffrc.h"
#include "xff/registry/descriptor.h"
#include "xff/registry/registry.h"

namespace xff::config {
namespace {

// The registry safety class of one flag token (kNone for globals/unknowns). An
// attached binding like "-capture=tag" is classified by its base name "-capture".
registry::Safety FlagSafety(std::string_view flag) {
  const std::string_view base = flag.substr(0, flag.find('='));
  const registry::Descriptor* const descriptor = registry::Lookup(base);
  return descriptor == nullptr ? registry::Safety::kNone : descriptor->safety;
}

// @safe/@sensitive/@destructive -> the matching class; nullopt if not a class token.
std::optional<registry::Safety> ClassToken(std::string_view token) {
  if (token == "@safe") {
    return registry::Safety::kNone;
  }
  if (token == "@destructive") {
    return registry::Safety::kSafety;
  }
  if (token == "@sensitive") {
    return registry::Safety::kSecurity;
  }
  return std::nullopt;
}

// Whether a [policy] rule `token` matches `line`: an @class token matches by the
// line's class; a flag-name token matches when any line flag equals it or carries
// it as an attached binding (flag == token, or flag starts with token + "=").
bool TokenMatchesLine(std::string_view token, const RcLine& line) {
  if (const std::optional<registry::Safety> cls = ClassToken(token); cls.has_value()) {
    return LineSafety(line) == *cls;
  }
  const std::string with_eq = absl::StrCat(token, "=");
  return absl::c_any_of(
      line.flags, [&](std::string_view flag) { return flag == token || absl::StartsWith(flag, with_eq); });
}

}  // namespace

registry::Safety LineSafety(const RcLine& line) {
  // Safety is declared in increasing restrictiveness (kNone < kSafety < kSecurity),
  // so the line's class is the maximum by enum value over its flags.
  registry::Safety worst = registry::Safety::kNone;
  for (const std::string& flag : line.flags) {
    const registry::Safety current = FlagSafety(flag);
    if (static_cast<int>(current) > static_cast<int>(worst)) {
      worst = current;
    }
  }
  return worst;
}

bool LinePermitted(const RcLine& line, Source layer, const SystemConfig& policy) {
  const registry::Safety cls = LineSafety(line);
  const bool builtin_allowed = layer != Source::kProject || cls == registry::Safety::kNone;
  const std::string_view layer_name = SourceName(layer);
  bool allow_override = false;
  bool deny_override = false;
  for (const PolicyRule& rule : policy.policy) {
    if (rule.layer != layer_name) {
      continue;
    }
    if (!absl::c_any_of(rule.tokens, [&](std::string_view token) { return TokenMatchesLine(token, line); })) {
      continue;
    }
    if (rule.allow) {
      allow_override = true;
    } else {
      deny_override = true;
    }
  }
  if (deny_override) {
    return false;  // deny beats allow
  }
  if (allow_override) {
    return true;  // a system rule loosens the built-in default
  }
  return builtin_allowed;
}

ConfigInputs GateConfig(const ConfigInputs& inputs, std::vector<Drop>* drops) {
  ConfigInputs gated = inputs;
  gated.user.clear();
  gated.project.clear();
  const auto gate = [&](const std::vector<RcLine>& lines, Source layer, std::vector<RcLine>& out) {
    for (const RcLine& line : lines) {
      if (LinePermitted(line, layer, inputs.system)) {
        out.push_back(line);
      } else if (drops != nullptr) {
        drops->push_back(Drop{.line = line, .layer = layer, .safety = LineSafety(line)});
      }
    }
  };
  gate(inputs.user, Source::kUser, gated.user);
  gate(inputs.project, Source::kProject, gated.project);
  return gated;
}

}  // namespace xff::config
