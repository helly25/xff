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

#ifndef XFF_CONFIG_XFFRC_H_
#define XFF_CONFIG_XFFRC_H_

#include <string>
#include <string_view>
#include <vector>

namespace xff::config {

// One parsed .xffrc line: an optional "<base>:<config>:" selector followed by
// flag tokens. `base` gates by the active style (empty or "common" applies
// always; otherwise "xff" / "find" / a custom style); `config` gates by an
// active --config=NAME (empty applies under any named config). The grammar
// mirrors bazel's rc files: "what you can type, you can save."
struct RcLine {
  std::string base;                // "" or "common" (always), else "xff" / "find" / a custom style
  std::string config;              // "" (any named config) or a specific --config=NAME
  std::vector<std::string> flags;  // the flag tokens after the selector
};

// Parses .xffrc `text` into its lines. Blank lines and lines whose first
// non-blank character is '#' are skipped. A line is
//   [<base>[:<config>]:] <flag>...
// where the optional leading selector token ends in ':'; everything after it is
// split on whitespace into flag tokens (a token that ends in ':' is treated as
// the selector, so a flag value like "--config=xff:2" is never mistaken for one
// because it does not end in ':'). Parse-only: nothing is interpreted, gated, or
// executed - that is the loader's and the policy gate's job. Flag values that
// contain whitespace (quoting) are a later refinement.
std::vector<RcLine> ParseXffrc(std::string_view text);

}  // namespace xff::config

#endif  // XFF_CONFIG_XFFRC_H_
