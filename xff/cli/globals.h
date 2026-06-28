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

#ifndef XFF_CLI_GLOBALS_H_
#define XFF_CLI_GLOBALS_H_

#include <string_view>

#include "absl/types/span.h"

namespace xff::cli {

// One whole-run option ("global"), the flag counterpart of registry::Descriptor.
// Globals are processed by config / main, not by the expression parser, so they
// live here rather than in the registry; the help system and the planned man-page /
// .md generators enumerate them through Globals() the way they enumerate primaries
// through registry::All(), so the documentation cannot drift from the binary.
struct GlobalFlag {
  std::string_view name;     // primary lookup key, e.g. "--sort", "--jobs", "-H"
  std::string_view alias;    // alternate lookup key, or "" (e.g. "-j" for --jobs, "--tz")
  std::string_view display;  // human header, e.g. "-j N, --jobs=N|all" or "--sort[=none|dir|subtree|tree]"
  std::string_view group;    // section for the index, e.g. "Config", "Traversal", "Output"
  std::string_view summary;  // one-line synopsis, lower-case, no trailing period
  bool xff = true;           // false for a find-native option (-H/-L/-P); true for an xff extension
};

// All global options, in display order. The single enumeration point for the help
// system and the planned doc generators.
absl::Span<const GlobalFlag> Globals();

// The global option named `name` (matching the canonical name or an alias), or
// nullptr if none. `name` carries its leading dashes (e.g. "--sort", "-j").
const GlobalFlag* LookupGlobal(std::string_view name);

}  // namespace xff::cli

#endif  // XFF_CLI_GLOBALS_H_
