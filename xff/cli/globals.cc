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
        .summary = "select a config style: find (strict), xff (evolved), rg (opinionated); repeatable",
        .details = "A config style sets the defaults for ignore files, hidden files, sizes, sort order, and case. "
                   "find is strict find compatibility; xff keeps find's grammar but sorts and prints human sizes; "
                   "rg is opinionated (respect .gitignore, skip hidden, smart case). Repeatable and "
                   "layered, last one wins. See --help=styles for the per-style defaults.",
        .topic = "config",
    },
    {
        .name = "--no-config",
        .display = "--no-config",
        .group = "config",
        .header = "Config",
        .summary = "ignore discovered .xffrc files",
        .topic = "config",
    },
    {
        .name = "--xffrc",
        .display = "--xffrc=FILE",
        .group = "config",
        .header = "Config",
        .summary = "also load a specific config file (a non-arming tier; see --allow-exec)",
        .details = "Loads FILE as a config tier above the user config (naming it is consent to LOAD it). It is a "
                   "NON-ARMING tier: safe directives apply, but a dangerous one - the exec family (-exec/-execdir/-ok, "
                   "-capture) or -delete - is inert unless --allow-exec is set from a trusted tier (the CLI or the "
                   "user/system config, never from an --xffrc file itself). An unarmed dangerous line is dropped with "
                   "a one-line warning. Repeatable; later files win.",
        .affects = "--allow-exec",
        .topic = "config",
    },
    {
        .name = "--allow-exec",
        .display = "--allow-exec",
        .group = "config",
        .header = "Config",
        .summary = "arm dangerous directives loaded from an --xffrc file (exec family, -delete)",
        .details = "Permits the sensitive/destructive directives (the exec family -exec/-execdir/-ok and -capture, "
                   "and the destructive -delete) carried by an --xffrc-loaded file to actually run. Honored only from "
                   "a trusted tier - typed on the CLI, or set in the user/system config - never from an --xffrc file "
                   "(so a named config cannot authorize itself). The root-owned system [policy] can hard-deny even "
                   "this. Without it, such lines are inert (dropped + warned); -delete still obeys its own "
                   "--safe/--dry-run guards.",
        .affects = "--xffrc",
        .topic = "config",
    },
    {
        .name = "--explain",
        .display = "--explain",
        .group = "config",
        .header = "Config",
        .summary = "print the resolved configuration and exit",
        .topic = "config",
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
        .name = "--archive",
        .display = "--archive",
        .group = "traversal",
        .header = "Traversal",
        .summary = "descend into archives (tar/zip/...) as virtual paths",
        .details = "Treats each archive (tar, gz, bzip2, xz, zstd, lz4, zip, ...) as a directory, so the whole "
                   "expression - including -grep on entry content - matches its entries at virtual paths like "
                   "`foo.tar.gz/inner/x`. Read-only. A build-time extra: the stock binary is lean and omits it "
                   "(rebuild with --//xff:archive); using --archive without it is a hard error.",
        .extra = "archive",
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
                   "per directory, while find and rg leave the order unspecified.",
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
        .summary = "letter case for matchers: -i insensitive, -s/-s+ smart, -s- sensitive (rg -> smart)",
        .details = "Controls case for -name/-path/-regex and the content matchers. sensitive matches exactly; "
                   "insensitive (-i) folds case; smart (-s / -s+) folds only when the pattern is all lower case and "
                   "matches exactly otherwise; -s- forces sensitive. rg defaults to smart.",
    },
    {
        .name = "--regextype",
        .display = "--regextype=RE2|EXACT|FNMATCH|GLOB|SHGLOB|PCRE2",
        .group = "matching",
        .header = "Matching",
        .summary = "match engine: RE2, EXACT, FNMATCH, GLOB, SHGLOB (GLOB + {a,b}), or PCRE2 (a build extra)",
        .details = "Selects the grammar for -regex/-iregex and the content matchers -rxc/-grep. RE2 (the "
                   "default) is linear-time regular expressions; EXACT is a literal string (metacharacters are "
                   "plain text); FNMATCH is a flat shell wildcard where * matches any character including /; "
                   "GLOB is a path-aware shell glob where */? stop at / and ** crosses directories (gitignore "
                   "semantics), with [...] classes; SHGLOB is GLOB plus {a,b} brace alternation, so *.{cc,h} "
                   "matches either. PCRE2 (Perl syntax: lookaround, backreferences) is the one build-time "
                   "extra: it is present only in a full build, and selecting it in a lean build is a hard "
                   "error, never a silent fall back to RE2. RE2/EXACT/FNMATCH/GLOB/SHGLOB are always built in; "
                   "run xff --help=extras to see whether THIS binary includes PCRE2. See --help=grammars for "
                   "a full description of each grammar (GLOB/SHGLOB are xff's own, not POSIX glob(7)).",
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
        .name = "--ignore-file",
        .display = "--ignore-file=PATH",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "read an extra gitignore-format file, rooted at its own directory (repeatable)",
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
        .summary = "include hidden dotfiles in the walk (default: find/xff show, rg skips)",
    },
    {
        .name = "--no-hidden",
        .display = "--no-hidden",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "skip hidden dotfiles (the rg default; opts find/xff out)",
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
        .affects = "-diff",
    },
    {
        .name = "--diff-ignore",
        .display = "--diff-ignore=TOKEN,...",
        .group = "output",
        .header = "Output",
        .summary = "normalize -diff comparison: ws, change, trail, blank, case, eofnl (comma-separated)",
        .affects = "-diff",
    },
    {
        .name = "--diff-ignore-matching",
        .display = "--diff-ignore-matching=REGEX",
        .group = "output",
        .header = "Output",
        .summary = "-diff ignores lines matching this regex (RE2)",
        .affects = "-diff",
    },
    {
        .name = "--diff-format",
        .display = "--diff-format=u|c|n|y",
        .group = "output",
        .header = "Output",
        .summary = "default -diff format: u/unified (default), c/context, n/normal, y/side-by-side",
        .affects = "-diff",
    },
    {
        .name = "--diff-context",
        .display = "--diff-context=N",
        .group = "output",
        .header = "Output",
        .summary = "default -diff context lines (3); overrides --context for -diff, and -diff=uN overrides it",
        .affects = "-diff",
    },
    {
        .name = "--hash-algorithm",
        .display = "--hash-algorithm=ALGO",
        .group = "output",
        .header = "Output",
        .summary = "default digest for -hash / {hash} (sha256 default; md5, sha512, blake3, and more)",
    },
    {
        .name = "--hash-encoding",
        .display = "--hash-encoding=hex|base64",
        .group = "output",
        .header = "Output",
        .summary = "default -hash / {hash} rendering: hex (default) or base64",
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
        .display = "--summary[=overall|type|ext|lang|mime|user|group]",
        .group = "output",
        .header = "Output",
        .summary = "print an aligned count + size table (or --format=jsonl rows) instead of each match",
        .details = "Replaces the per-match listing with an aggregate table: match count and total size per group "
                   "(overall, by type, extension, programming language, media (MIME) type, user (owner), or "
                   "owning group). The categorical keys reuse the {mime}/{user}/{group} field vocabulary. "
                   "--top=N limits the rows, --summary-precision sets the scaled-size digits, and --format=jsonl "
                   "emits one object per group for scripts.",
        .topic = "stats",
    },
    {
        .name = "--histogram",
        .display = "--histogram=BUCKET[:MEASURE]",
        .group = "output",
        .header = "Output",
        .summary = "bar chart per bucket: a count or sum/mean/min/max of size|lines (repeatable)",
        .details = "A terminal reduction like --summary, drawn as bars. BUCKET groups the matches - a category "
                   "(overall, type, ext, lang, mime, user (owner), or group) or a numeric-range field "
                   "(size / lines by order of magnitude, depth per level, drawn as an ascending distribution). "
                   "The optional :MEASURE is the bar's value - "
                   "`count` (the default) or an aggregate "
                   "`sum(FIELD)` / `mean(FIELD)` / `min(FIELD)` / `max(FIELD)` over a numeric FIELD (size or lines). "
                   "A numeric metric needs an aggregator (`ext:lines` is an error; `ext:sum(lines)` is not). "
                   "Repeatable and combinable with --summary - both are fed by one walk and replace the per-match "
                   "listing. Bars scale to the tallest, use Unicode block characters on a UTF-8 locale (see "
                   "--unicode) or ASCII '#' otherwise; --top=N keeps the N tallest and --format=jsonl emits one "
                   "object per bar for scripts.",
        .topic = "stats",
    },
    {
        .name = "--count",
        .alias = "-c",
        .display = "--count, -c",
        .group = "output",
        .header = "Output",
        .summary = "with -grep, print a per-file matching-line count (path:count) instead of the lines",
        .affects = "-grep",
    },
    {
        .name = "--context",
        .display = "--context=SPEC",
        .group = "output",
        .header = "Output",
        .summary = "-grep context lines: N both sides, or A:N,B:N,C:N for after/before/both (grep -C/-A/-B)",
        .affects = "-grep,-diff,--diff-context",
    },
    {
        .name = "--after-context",
        .display = "--after-context=N",
        .group = "output",
        .header = "Output",
        .summary = "with -grep, print N lines of context after each match (grep -A; = --context=A:N)",
        .affects = "-grep",
    },
    {
        .name = "--before-context",
        .display = "--before-context=N",
        .group = "output",
        .header = "Output",
        .summary = "with -grep, print N lines of context before each match (grep -B; = --context=B:N)",
        .affects = "-grep",
    },
    {
        .name = "--top",
        .display = "--top=N",
        .group = "output",
        .header = "Output",
        .summary = "with --summary or --histogram, keep only the N largest/tallest groups",
        .topic = "stats",
    },
    {
        .name = "--histogram-width",
        .display = "--histogram-width=N",
        .group = "output",
        .header = "Output",
        .summary = "cell width the tallest --histogram bar fills (default 40)",
        .affects = "--histogram",
        .topic = "stats",
    },
    {
        .name = "--summary-precision",
        .display = "--summary-precision=N",
        .group = "output",
        .header = "Output",
        .summary = "with --summary --human: fraction digits for scaled sizes (default 2; bytes stay integer)",
        .topic = "stats",
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
        .display = "--human[=si|iec|off]",
        .group = "output",
        .header = "Output",
        .summary = "size units for -ls / --summary: si (kB/MB, default), iec (KiB/MiB), off (bytes); xff -> si",
    },
    {
        .name = "--si",
        .display = "--si",
        .group = "output",
        .header = "Output",
        .summary = "human sizes in SI (kB/MB, 1000^N); an alias for --human=si (the --human default)",
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

bool ExtraEnabled(std::string_view key) {
  // Each build-time extra maps to an XFF_WITH_* define added (via select) by its `//xff:<extra>`
  // Bazel flag. In the lean default build no such define is set, so every extra reads as off. New
  // extras add a branch here (and #83 wires libarchive behind archive).
  if (key == "archive") {
#ifdef XFF_WITH_ARCHIVE
    return true;
#else
    return false;
#endif
  }
  return false;  // unknown / not-yet-wired extra
}

}  // namespace xff::cli
