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
//
// Each element carries a trailing comma so clang-format lays the whole table out
// one field per line, uniformly (see STYLE_CPP.md "long struct-array tables").
constexpr std::array kGlobals = std::to_array<GlobalFlag>({
    {
        .name = "--config",
        .display = "--config=NAME",
        .group = "config",
        .header = "Config",
        .summary = "select a config style: find (strict), xff (evolved), xfd/rg (opinionated); repeatable",
        .details = "A config style sets the defaults for ignore files, hidden files, sizes, sort order, and case. "
                   "find is strict find compatibility; xff keeps find's grammar but sorts and prints human sizes; "
                   "xfd and rg are opinionated (respect .gitignore, skip hidden, smart case). Repeatable and "
                   "layered, last one wins. See --help=styles for the per-style defaults.",
    },
    {
        .name = "--no-config",
        .display = "--no-config",
        .group = "config",
        .header = "Config",
        .summary = "ignore discovered .xffrc files",
    },
    {
        .name = "--xffrc",
        .display = "--xffrc=FILE",
        .group = "config",
        .header = "Config",
        .summary = "also load a specific config file",
    },
    {
        .name = "--explain",
        .display = "--explain",
        .group = "config",
        .header = "Config",
        .summary = "print the resolved configuration and exit",
    },
    {
        .name = "-H",
        .display = "-H",
        .group = "traversal",
        .header = "Traversal",
        .summary = "follow symlinks named on the command line, not while walking",
        .xff = false,
    },
    {
        .name = "-L",
        .display = "-L",
        .group = "traversal",
        .header = "Traversal",
        .summary = "follow symlinks everywhere during the walk",
        .xff = false,
    },
    {
        .name = "-P",
        .display = "-P",
        .group = "traversal",
        .header = "Traversal",
        .summary = "never follow symlinks (the default)",
        .xff = false,
    },
    {
        .name = "--jobs",
        .alias = "-j",
        .display = "-j N, --jobs=N|all",
        .group = "traversal",
        .header = "Traversal",
        .summary = "worker count for the walk and concurrent -exec (all = every core)",
    },
    {
        .name = "--sort",
        .display = "--sort[=none|dir|subtree|tree]",
        .group = "traversal",
        .header = "Traversal",
        .summary = "sibling/traversal ordering (default depends on the mode)",
        .details = "none leaves entries in filesystem order (fastest); dir sorts each directory's entries; subtree "
                   "and tree give a deterministic order across the whole walk. The default is per style: xff sorts "
                   "per directory, while find, xfd, and rg leave the order unspecified.",
    },
    {
        .name = "--block-size",
        .display = "--block-size=SIZE",
        .group = "matching",
        .header = "Matching",
        .summary = "bytes per -size block for a bare -size N / -size Nb (default 512)",
    },
    {
        .name = "--exact",
        .display = "--exact",
        .group = "matching",
        .header = "Matching",
        .summary = "match -name/-path byte-exact, opting out of the xff FS-native case default",
    },
    {
        .name = "--case",
        .display = "--case=sensitive|insensitive|smart, -i, -s[+|-]",
        .group = "matching",
        .header = "Matching",
        .summary = "letter case for matchers: -i insensitive, -s/-s+ smart, -s- sensitive (xfd/rg -> smart)",
        .details = "Controls case for -name/-path/-regex and the content matchers. sensitive matches exactly; "
                   "insensitive (-i) folds case; smart (-s / -s+) folds only when the pattern is all lower case and "
                   "matches exactly otherwise; -s- forces sensitive. xfd and rg default to smart.",
    },
    {
        .name = "--regextype",
        .display = "--regextype=RE2|EXACT",
        .group = "matching",
        .header = "Matching",
        .summary = "how -grep reads its pattern: RE2 regex (default) or EXACT literal (MATCH/PCRE reserved)",
    },
    {
        .name = "--exclude",
        .display = "--exclude=GLOB",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "skip paths matching a gitignore-style glob (repeatable; a matched directory is pruned)",
    },
    {
        .name = "--include",
        .display = "--include=GLOB",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "re-include paths a --exclude would skip, matching a gitignore-style glob (repeatable)",
    },
    {
        .name = "--gitignore",
        .alias = "-g",
        .display = "--gitignore[=on|off], -g[+|-]",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "respect .gitignore files: -g = auto (only in a git repo), -g+/=on always, -g-/=off never",
        .details = "Reads .gitignore rules while walking, including nested .gitignore files, .git/info/exclude, and "
                   "core.excludesFile. -g / auto activates only inside a git working tree; -g+ / =on forces it "
                   "anywhere; -g- / =off disables it. Independent of --ignore-files (.ignore / .xffignore).",
    },
    {
        .name = "--ignore-files",
        .display = "--ignore-files",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "respect per-directory .ignore and .xffignore files (off by default)",
    },
    {
        .name = "--no-ignore",
        .alias = "-u",
        .display = "--no-ignore, -u",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "disable all ignore-file processing (.gitignore/.ignore/.xffignore)",
    },
    {
        .name = "--hidden",
        .display = "--hidden",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "include hidden dotfiles in the walk (default: find/xff show, xfd/rg skip)",
    },
    {
        .name = "--no-hidden",
        .display = "--no-hidden",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "skip hidden dotfiles (the xfd/rg default; opts find/xff out)",
    },
    {
        .name = "--format",
        .display = "--format=plain|nul|jsonl|csv|tsv|aligned|markdown|tree",
        .group = "output",
        .header = "Output",
        .summary = "output format: plain, nul, jsonl, csv, tsv, aligned, markdown (md), tree; default plain",
    },
    {
        .name = "--no-header",
        .display = "--no-header",
        .group = "output",
        .header = "Output",
        .summary = "omit the header row from tabular --format (csv/tsv/aligned/markdown; on by default)",
    },
    {
        .name = "--columns",
        .display = "--columns=FIELD,...",
        .group = "output",
        .header = "Output",
        .summary = "columns for tabular --format, from the {field} vocabulary (e.g. path,size,mtime)",
    },
    {
        .name = "--diff-algorithm",
        .display = "--diff-algorithm=naive|direct|myers",
        .group = "output",
        .header = "Output",
        .summary = "diff engine for -diff: naive, direct, or myers (the default, minimal like git)",
    },
    {
        .name = "--path-encoding",
        .display = "--path-encoding=raw|escape",
        .group = "output",
        .header = "Output",
        .summary = "plain-output path byte encoding: raw (verbatim, default) or escape (C-escape controls)",
    },
    {
        .name = "--template",
        .display = "--template=TEMPLATE",
        .group = "output",
        .header = "Output",
        .summary = "render each match through a field template ({path}, {name}, ...)",
    },
    {
        .name = "--implicit-print",
        .display = "--implicit-print=yes|no",
        .group = "output",
        .header = "Output",
        .summary = "force the default -print on or off",
    },
    {
        .name = "--summary",
        .display = "--summary[=overall|type|ext]",
        .group = "output",
        .header = "Output",
        .summary = "print an aligned count + size table (or --format=jsonl rows) instead of each match",
        .details = "Replaces the per-match listing with an aggregate table: match count and total size per group "
                   "(overall, by type, or by extension). --top=N limits the rows, --summary-precision sets the "
                   "scaled-size digits, and --format=jsonl emits one object per group for scripts.",
    },
    {
        .name = "--count",
        .alias = "-c",
        .display = "--count, -c",
        .group = "output",
        .header = "Output",
        .summary = "with -grep, print a per-file matching-line count (path:count) instead of the lines",
    },
    {
        .name = "--top",
        .display = "--top=N",
        .group = "output",
        .header = "Output",
        .summary = "with --summary, show only the N largest groups by size (the total row still counts all)",
    },
    {
        .name = "--summary-precision",
        .display = "--summary-precision=N",
        .group = "output",
        .header = "Output",
        .summary = "with --summary --human: fraction digits for scaled sizes (default 2; bytes stay integer)",
    },
    {
        .name = "--color",
        .display = "--color[=auto|always|never]",
        .group = "output",
        .header = "Output",
        .summary = "colorize the plain listing by file type: auto (a tty), always, or never; honors NO_COLOR",
        .details = "Colorizes the plain listing by file type. auto colorizes only when stdout is a terminal; always "
                   "forces color even through a pipe or pager; never disables it. The NO_COLOR environment variable "
                   "always wins.",
    },
    {
        .name = "--unicode",
        .display = "--unicode[=auto|always|never]",
        .group = "output",
        .header = "Output",
        .summary = "--format=tree connectors: auto (a UTF-8 locale), always (Unicode), or never (ASCII)",
        .details = "Selects the box-drawing characters --format=tree connects nodes with. auto uses Unicode when the "
                   "locale (LC_ALL / LC_CTYPE / LANG) is UTF-8, else ASCII; always forces the Unicode connectors; "
                   "never forces the ASCII ones.",
    },
    {
        .name = "--human",
        .display = "--human[=iec|si|off]",
        .group = "output",
        .header = "Output",
        .summary = "size units for -ls / --summary: iec (KiB/MiB), si (kB/MB), off (bytes); xff -> iec",
    },
    {
        .name = "--buffer",
        .display = "--buffer[=auto|off|all|N[kMG]|NMB]",
        .group = "output",
        .header = "Output",
        .summary = "buffer to size columns (-ls / tables): auto, off, all, N[kMG] rows, or NMB/NMiB bytes",
    },
    {
        .name = "--quiet",
        .alias = "-q",
        .display = "--quiet, -q",
        .group = "exit",
        .header = "Exit code control",
        .summary = "suppress output; exit 0 if anything matched, else 1 (-q: grep-compatible)",
    },
    {
        .name = "--exit-match",
        .display = "--exit-match",
        .group = "exit",
        .header = "Exit code control",
        .summary = "keep output; exit 0 if anything matched, else 1",
    },
    {
        .name = "--safe",
        .display = "--safe",
        .group = "safety",
        .header = "Safety",
        .summary = "refuse destructive actions (-delete / -exec)",
    },
    {
        .name = "--dry-run",
        .display = "--dry-run",
        .group = "safety",
        .header = "Safety",
        .summary = "preview -delete without removing anything",
    },
    {
        .name = "--skip-unsupported",
        .display = "--skip-unsupported",
        .group = "safety",
        .header = "Safety",
        .summary = "warn and skip a predicate a filesystem cannot evaluate, not fail",
    },
    {
        .name = "--exec-fields",
        .display = "--exec-fields",
        .group = "fields",
        .header = "Fields & Exec",
        .summary = "render -exec tokens through the field vocabulary ({name}, {path}, ...)",
    },
    {
        .name = "--define",
        .display = "--define=NAME=VALUE",
        .group = "fields",
        .header = "Fields & Exec",
        .summary = "define a value referenced as {def.NAME}",
    },
    {
        .name = "--capture-override",
        .display = "--capture-override",
        .group = "fields",
        .header = "Fields & Exec",
        .summary = "allow a -capture NAME to be bound more than once (last wins)",
    },
    {
        .name = "--time-format",
        .display = "--time-format=FMT",
        .group = "time",
        .header = "Time",
        .summary = "default format for time fields (a preset name or a strftime pattern)",
        .details = "Sets the default rendering for time fields ({mtime}, {atime}, -printf %t, ...) when no per-field "
                   "qualifier is given. Accepts a preset (iso, epoch, space, find) or any strftime pattern such as "
                   "%Y-%m-%d. A per-field qualifier like {mtime:%H:%M} still overrides it.",
    },
    {
        .name = "--timezone",
        .alias = "--tz",
        .display = "--timezone=ZONE, --tz=ZONE",
        .group = "time",
        .header = "Time",
        .summary = "zone for interpreting/formatting times (local, utc, an IANA name, or +HH:MM)",
        .details = "The zone used to interpret and format every time. Accepts local, utc, an IANA name like "
                   "Europe/London, or a fixed offset like +02:00. Affects time fields and -newerXt comparisons.",
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

bool IsKnownGlobal(std::string_view arg) {
  // Compat aliases that are not table rows: -0 (= --format=nul), the -g+/-g- short
  // gitignore forms (= --gitignore=on/off), and the short case forms -i (insensitive),
  // -s/-s+ (smart), -s- (sensitive) (= --case=...).
  if (arg == "-0" || arg == "-g+" || arg == "-g-" || arg == "-i" || arg == "-s" || arg == "-s+" || arg == "-s-") {
    return true;
  }
  // The short jobs form carries its value attached: -j4, -jall (the "=" form --jobs=N
  // is handled by the valued-name path below via the -j alias).
  if (arg.starts_with("-j") && arg.size() > 2) {
    return true;
  }
  // An exact name or alias (bare flags, and valued flags used without a value).
  if (LookupGlobal(arg) != nullptr) {
    return true;
  }
  // A valued form name=VALUE / alias=VALUE: the key must resolve to a flag that
  // advertises a value (its display contains '='), so `--safe=x` stays unknown while
  // `--sort=tree` / `--define=A=B` are accepted (only the key before the first '=').
  if (const std::string_view::size_type eq = arg.find('='); eq != std::string_view::npos) {
    const GlobalFlag* const flag = LookupGlobal(arg.substr(0, eq));
    return flag != nullptr && flag->display.find('=') != std::string_view::npos;
  }
  return false;
}

}  // namespace xff::cli
