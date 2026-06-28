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

#include "xff/cli/globals.h"

#include <array>
#include <string_view>

#include "absl/types/span.h"

namespace xff::cli {
namespace {

// The whole-run options, in the order the --help usage page groups them. `--help` /
// `--version` and their aliases are deliberately omitted: they are special-cased in
// main.cc and self-evident, and `--help=--help` would be circular.
constexpr std::array kGlobals = std::to_array<GlobalFlag>({
    {
        .name = "--config",
        .display = "--config=NAME",
        .group = "Config",
        .summary = "select a style or config layer (find = strict find, xff = modern); repeatable",
    },
    {.name = "--no-config", .display = "--no-config", .group = "Config", .summary = "ignore discovered .xffrc files"},
    {.name = "--xffrc", .display = "--xffrc=FILE", .group = "Config", .summary = "also load a specific config file"},
    {.name = "--explain",
     .display = "--explain",
     .group = "Config",
     .summary = "print the resolved configuration and exit"},
    {.name = "-H",
     .display = "-H",
     .group = "Traversal",
     .summary = "follow symlinks named on the command line, not while walking",
     .xff = false},
    {.name = "-L",
     .display = "-L",
     .group = "Traversal",
     .summary = "follow symlinks everywhere during the walk",
     .xff = false},
    {.name = "-P",
     .display = "-P",
     .group = "Traversal",
     .summary = "never follow symlinks (the default)",
     .xff = false},
    {
        .name = "--jobs",
        .alias = "-j",
        .display = "-j N, --jobs=N|all",
        .group = "Traversal",
        .summary = "worker count for the walk and concurrent -exec (all = every core)",
    },
    {
        .name = "--sort",
        .display = "--sort[=none|dir|subtree|tree]",
        .group = "Traversal",
        .summary = "sibling/traversal ordering (default depends on the mode)",
    },
    {
        .name = "--block-size",
        .display = "--block-size=SIZE",
        .group = "Matching",
        .summary = "bytes per -size block for a bare -size N / -size Nb (default 512)",
    },
    {
        .name = "--format",
        .display = "--format=plain|nul|jsonl",
        .group = "Output",
        .summary = "record format (plain default; nul = -print0; jsonl = JSON lines)",
    },
    {
        .name = "--template",
        .display = "--template=TEMPLATE",
        .group = "Output",
        .summary = "render each match through a field template ({path}, {name}, ...)",
    },
    {.name = "--implicit-print",
     .display = "--implicit-print=yes|no",
     .group = "Output",
     .summary = "force the default -print on or off"},
    {
        .name = "--summary",
        .display = "--summary[=overall|type|ext]",
        .group = "Output",
        .summary = "print a count + size table instead of each match",
    },
    {.name = "--quiet",
     .display = "--quiet",
     .group = "Exit by match",
     .summary = "suppress output; exit 0 if anything matched, else 1"},
    {.name = "--exit-match",
     .display = "--exit-match",
     .group = "Exit by match",
     .summary = "keep output; exit 0 if anything matched, else 1"},
    {.name = "--safe",
     .display = "--safe",
     .group = "Safety",
     .summary = "refuse destructive actions (-delete / -exec)"},
    {.name = "--dry-run",
     .display = "--dry-run",
     .group = "Safety",
     .summary = "preview -delete without removing anything"},
    {
        .name = "--skip-unsupported",
        .display = "--skip-unsupported",
        .group = "Safety",
        .summary = "warn and skip a predicate a filesystem cannot evaluate, not fail",
    },
    {
        .name = "--exec-fields",
        .display = "--exec-fields",
        .group = "Fields & exec",
        .summary = "render -exec tokens through the field vocabulary ({name}, {path}, ...)",
    },
    {.name = "--define",
     .display = "--define=NAME=VALUE",
     .group = "Fields & exec",
     .summary = "define a value referenced as {def.NAME}"},
    {
        .name = "--capture-override",
        .display = "--capture-override",
        .group = "Fields & exec",
        .summary = "allow a -capture NAME to be bound more than once (last wins)",
    },
    {
        .name = "--time-format",
        .display = "--time-format=FMT",
        .group = "Time",
        .summary = "default format for time fields (a preset name or a strftime pattern)",
    },
    {
        .name = "--timezone",
        .alias = "--tz",
        .display = "--timezone=ZONE, --tz=ZONE",
        .group = "Time",
        .summary = "zone for interpreting/formatting times (local, utc, an IANA name, or +HH:MM)",
    },
});

}  // namespace

absl::Span<const GlobalFlag> Globals() {
  return kGlobals;
}

const GlobalFlag* LookupGlobal(std::string_view name) {
  for (const GlobalFlag& flag : kGlobals) {
    if (flag.name == name || (!flag.alias.empty() && flag.alias == name)) {
      return &flag;
    }
  }
  return nullptr;
}

}  // namespace xff::cli
