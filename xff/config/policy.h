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

#ifndef XFF_CONFIG_POLICY_H_
#define XFF_CONFIG_POLICY_H_

#include <string>
#include <vector>

#include "xff/config/config.h"
#include "xff/config/ini.h"
#include "xff/config/xffrc.h"
#include "xff/registry/descriptor.h"

namespace xff::config {

// The safety class of a whole .xffrc line: the most restrictive class among its
// flags (registry Lookup; unknown tokens such as globals are kNone). A line
// counts as sensitive/destructive when ANY of its flags is. An attached binding
// like "-capture=tag" is classified by its base name before '='.
registry::Safety LineSafety(const RcLine& line);

// Whether a `line` from `layer` is permitted under the system `policy`. The
// safe-by-default built-in table denies the project layer any kSecurity (the
// exec family) or kSafety (-delete) line; the user and system layers may do
// anything. The [policy] rules then override per layer (deny beats allow):
// tightening a safe flag or loosening a sensitive one, addressed by flag name or
// an @safe/@sensitive/@destructive class token. Only the root-owned system layer
// supplies [policy].
bool LinePermitted(const RcLine& line, Source layer, const SystemConfig& policy);

// Why the gate dropped a line: a safety-policy denial (its safety class bars it from the layer),
// or a structural rule (it attaches behavior to a built-in preset, which no config file may do).
enum class DropReason { kSafetyPolicy, kPresetOverload };

// A config line dropped by the gate: the line, the layer it came from, the safety class (relevant
// to kSafetyPolicy), and why it was dropped (for the stderr warning and --explain).
struct Drop {
  RcLine line;
  Source layer;
  registry::Safety safety;
  DropReason reason = DropReason::kSafetyPolicy;
};

// Whether `line` attaches behavior to a built-in preset: its base names a style (find/xff/rg/xfd)
// with no named-config component (`xff: --flag`), so it would apply whenever that preset is active
// and silently change what a plain `xff` run does. Config files customize via `common:` (always-on)
// or named blocks (`myconfig:`, or the style-scoped `xff:myconfig:`) instead, so a preset stays
// reproducible. Applies to every layer.
bool OverloadsPreset(const RcLine& line);

// Filters the user and project .xffrc lines of `inputs` through LinePermitted
// (with inputs.system as the policy), returning a copy with the denied lines
// removed and recording each in `drops` (when non-null). The system [defaults]
// are root-authored and never gated; CLI flags are not config and never gated.
ConfigInputs GateConfig(const ConfigInputs& inputs, std::vector<Drop>* drops);

// A one-line human description of a dropped line for the stderr warning and
// --explain, e.g. "'-exec' from the project .xffrc (sensitive)".
std::string DropMessage(const Drop& drop);

}  // namespace xff::config

#endif  // XFF_CONFIG_POLICY_H_
