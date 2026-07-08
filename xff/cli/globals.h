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
  std::string_view group;    // short group key, e.g. "config", "traversal", "filter" (drives grouping)
  std::string_view header;   // display heading for the group, e.g. "Filter & Ignore" (shown at its first flag)
  std::string_view summary;  // one-line synopsis, lower-case, no trailing period
  // Optional multi-sentence explanation shown by `--help=NAME` and `--help=full` (the
  // long tier); empty falls back to the summary. `--help=all` shows the summary only.
  std::string_view details;
  // The single source of truth for cross-references in the help system: a comma-separated
  // list of the expression primaries and other global flags whose behavior this flag changes
  // (e.g. "-diff" for --diff-format, "-grep,-diff" for --context). Detailed help derives an
  // "Affects:" block here and the reverse "Affected by:" block on each named entry, so the two
  // directions cannot drift (globals_test checks every token resolves). Each token is a primary
  // name ("-diff") or a global name ("--context"); a leading-'@' category token is a planned
  // extension (not yet resolved) for when one flag affects a whole family at once.
  std::string_view affects;
  // Help-topic membership, e.g. "stats". A `--help=TOPIC` body pulls every flag tagged with its
  // topic straight from this table, so the topic's flag list is the SOT and cannot drift. Empty
  // means the flag belongs to no topic body (it still appears in the grouped `--help` options).
  std::string_view topic;
  // The build-time "composable extra" this flag needs, e.g. "archive" (the `//xff:archive` Bazel
  // flag + its `XFF_WITH_ARCHIVE` define). Empty = a core flag, always available. When set and that
  // extra is NOT compiled in (ExtraEnabled), the flag stays listed but is a hard error if used, and
  // the help system routes it into a separate "Extras (not built in)" group noting what to rebuild
  // with. The key alone is the SOT; the `--//xff:<extra>` hint is derived from it.
  std::string_view extra;
  bool xff = true;  // false for a find-native option (-H/-L/-P); true for an xff extension
};

// Whether the build-time extra `key` (e.g. "archive") is compiled into this binary. Reads the
// `XFF_WITH_*` defines the Bazel `//xff:<extra>` flags add via select(); an unknown key is false.
// The single point that maps an extra key to its compile-time availability, shared by main (the
// used-but-unavailable hard error) and the help system (the "Extras (not built in)" grouping).
bool ExtraEnabled(std::string_view key);

// All global options, in display order. The single enumeration point for the help
// system and the planned doc generators.
absl::Span<const GlobalFlag> Globals();

// The global option named `name` (matching the canonical name or an alias), or
// nullptr if none. `name` carries its leading dashes (e.g. "--sort", "-j").
const GlobalFlag* LookupGlobal(std::string_view name);

// Whether `arg` is a recognized whole-run global token, so `main` can reject an
// unknown leading option instead of silently ignoring it. Accepts: an exact name or
// alias; a valued `name=VALUE` / `alias=VALUE` form when the flag advertises a value
// (its `display` contains '='); the `-jN` / `-jall` short jobs form; and the compat
// aliases not in the table (`-0`, `-g+`, `-g-`). The meta flags `--help` / `--version`
// / `--man` / `--markdown` are consumed before parsing and are not checked here.
bool IsKnownGlobal(std::string_view arg);

}  // namespace xff::cli

#endif  // XFF_CLI_GLOBALS_H_
