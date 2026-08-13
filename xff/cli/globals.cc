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

#include "absl/strings/match.h"
#include "absl/types/span.h"
#include "xff/archive/archive_backend.h"

namespace xff::cli {
namespace {

// Allowed-value tables for the flags whose synopsis collapses a long value grammar to a
// `<PLACEHOLDER>` (e.g. `--summary[=<GROUP>]`). Rendered by the detail tier as an aligned,
// wrapping `value  meaning` table, so the values stay documented off the synopsis line.
constexpr std::array kCaseValues = std::to_array<ValueDoc>({
    {.value = "sensitive", .meaning = "match exactly (-s-)"},
    {.value = "insensitive", .meaning = "fold case (-i)"},
    {.value = "smart", .meaning = "fold case only when the pattern is all lower case (-s / -s+)"},
});
constexpr std::array kRegextypeValues = std::to_array<ValueDoc>({
    {.value = "RE2", .meaning = "linear-time regular expressions (the default)"},
    {.value = "EXACT", .meaning = "a literal string; metacharacters are plain text"},
    {.value = "FNMATCH", .meaning = "flat shell wildcard; `*` matches any character including `/`"},
    {.value = "GLOB", .meaning = "path-aware shell glob; `*`/`?` stop at `/`, `**` crosses directories"},
    {.value = "SHGLOB", .meaning = "GLOB plus `{a,b}` brace alternation, so `*.{cc,h}` matches either"},
    {.value = "PCRE2", .meaning = "Perl syntax (lookaround, backreferences); a build extra"},
});
constexpr std::array kSkipVcsValues = std::to_array<ValueDoc>({
    {.value = "git", .meaning = ".git"},
    {.value = "hg", .meaning = ".hg"},
    {.value = "svn", .meaning = ".svn"},
    {.value = "jj", .meaning = ".jj"},
    {.value = "bzr", .meaning = ".bzr"},
    {.value = "darcs", .meaning = "_darcs"},
    {.value = "cvs", .meaning = "CVS"},
    {.value = "all", .meaning = "every known VCS (the bare-flag default)"},
    {.value = "none", .meaning = "off (same as --no-skip-vcs)"},
});
constexpr std::array kFormatValues = std::to_array<ValueDoc>({
    {.value = "plain", .meaning = "one path per line (the default)"},
    {.value = "nul", .meaning = "NUL-separated paths (for xargs -0)"},
    {.value = "jsonl", .meaning = "one JSON object per match"},
    {.value = "csv", .meaning = "comma-separated columns"},
    {.value = "tsv", .meaning = "tab-separated columns"},
    {.value = "aligned", .meaning = "column-aligned table"},
    {.value = "markdown", .meaning = "a Markdown table (also `md`)"},
    {.value = "tree", .meaning = "an indented directory tree"},
});
constexpr std::array kSummaryValues = std::to_array<ValueDoc>({
    {.value = "overall", .meaning = "one row aggregated over all matches"},
    {.value = "type", .meaning = "by file type"},
    {.value = "ext", .meaning = "by extension"},
    {.value = "lang", .meaning = "by programming language"},
    {.value = "mime", .meaning = "by media (MIME) type"},
    {.value = "user", .meaning = "by owner"},
    {.value = "group", .meaning = "by owning group"},
    {.value = "hash", .meaning = "by file digest (dedup: identical files share a bucket; reads every file)"},
    {.value = "{template}", .meaning = "by any field value, e.g. `--summary='{ext}-{type}'`"},
});
constexpr std::array kArchiveValues = std::to_array<ValueDoc>({
    {.value = "none", .meaning = "an archive is one plain file (find behavior; the find-style default)"},
    {.value = "roots", .meaning = "dive only when a search root is itself an archive (the xff-family default)"},
    {.value = "all", .meaning = "also dive archives found during the walk (what bare `--archive` selects)"},
});
constexpr std::array kColorSchemeValues = std::to_array<ValueDoc>({
    {.value = "auto",
     .meaning = "ls OR xff: the theme when $LS_COLORS / $LSCOLORS is set, else xff's scheme (the "
                "default; also spelled `ls+xff`, `ls-or-xff` or `default`)"},
    {.value = "ls", .meaning = "the theme alone ($LS_COLORS, else $LSCOLORS): what it omits prints plain, as in ls"},
    {.value = "merged",
     .meaning = "the theme where it speaks, xff's colour for every key it omits, per key (also `ls-and-xff`)"},
    {.value = "xff", .meaning = "xff's built-in type scheme, ignoring $LS_COLORS"},
});
constexpr std::array kArchiveAggregateValues = std::to_array<ValueDoc>({
    {.value = "members", .meaning = "count what is INSIDE a dived container, not the container (the default)"},
    {.value = "container", .meaning = "count containers as the files they are on disk, never their members"},
    {.value = "both", .meaning = "count each container AND its members - the archive plus its unpacked copy"},
});
constexpr std::array kArchivePrefixValues = std::to_array<ValueDoc>({
    {.value = "(empty)", .meaning = "no prefix - a bare path, `a.tgz!inner/x` (the default)"},
    {.value = "URI", .meaning = "`archive:///abs/a.tar!x` when the container is absolute, else `archive:a.tgz!x`"},
    {.value = "STRING", .meaning = "any other value is used literally, e.g. `--archive-prefix=vfs:`"},
});
constexpr std::array kShardsValues = std::to_array<ValueDoc>({
    {.value = "auto", .meaning = "recognize every built-in scheme (the default when bare `--shards`)"},
    {.value = "of", .meaning = "only `<stem>-<index>-of-<total>` (TFRecord-style)"},
    {.value = "dotnum", .meaning = "only `<stem>.<NNN>` (7-Zip-style volumes)"},
    {.value = "underscore", .meaning = "only `<stem>_<NNN>`"},
});
constexpr std::array kShardsShowValues = std::to_array<ValueDoc>({
    {.value = "first", .meaning = "the representative (lowest-index) shard's path (the default)"},
    {.value = "wildcard", .meaning = "the masked-index name, e.g. `arc.???` (or `f-` idx `-of-003`)"},
    {.value = "count", .meaning = "the wildcard name plus the shard count, e.g. `arc.??? (3 shards)`"},
});
constexpr std::array kShardsDedupValues = std::to_array<ValueDoc>({
    {.value = "first", .meaning = "keep the lexicographically-first name among same-index copies (the default)"},
    {.value = "mtime", .meaning = "keep the newest by modification time (ties break on name)"},
    {.value = "error", .meaning = "treat a same-index duplicate as an error (non-zero exit)"},
});
constexpr std::array kPagerValues = std::to_array<ValueDoc>({
    {.value = "auto", .meaning = "page the help / man / markdown output on a terminal (the default)"},
    {.value = "always", .meaning = "page that meta output even through a pipe"},
    {.value = "all", .meaning = "auto, plus the file listing (on a terminal)"},
    {.value = "never", .meaning = "never page (same as --no-pager)"},
});
constexpr std::array kZoneSuffixValues = std::to_array<ValueDoc>({
    {.value = "auto", .meaning = "each format's built-in default (the default)"},
    {.value = "always", .meaning = "force the offset, even on a format that omits it (also true / yes / on)"},
    {.value = "never", .meaning = "drop the optional offset (also false / no / off)"},
});
// The digest names, in xff/hash's sorted AlgorithmNames() order; a globals_test guard keeps
// this list identical to that SOT so it cannot drift.
constexpr std::array kHashAlgorithmValues = std::to_array<ValueDoc>({
    {.value = "blake2b", .meaning = "BLAKE2b, 512-bit"},
    {.value = "blake2b_256", .meaning = "BLAKE2b, 256-bit"},
    {.value = "blake3", .meaning = "BLAKE3 (fast, parallel)"},
    {.value = "md5", .meaning = "128-bit legacy (fast, collision-broken)"},
    {.value = "sha1", .meaning = "160-bit legacy (collision-broken)"},
    {.value = "sha224", .meaning = "SHA-2, 224-bit"},
    {.value = "sha256", .meaning = "SHA-2, 256-bit (the default)"},
    {.value = "sha384", .meaning = "SHA-2, 384-bit"},
    {.value = "sha3_224", .meaning = "SHA-3 (Keccak), 224-bit"},
    {.value = "sha3_256", .meaning = "SHA-3 (Keccak), 256-bit"},
    {.value = "sha3_384", .meaning = "SHA-3 (Keccak), 384-bit"},
    {.value = "sha3_512", .meaning = "SHA-3 (Keccak), 512-bit"},
    {.value = "sha512", .meaning = "SHA-2, 512-bit"},
    {.value = "sha512_224", .meaning = "SHA-2, 512/224 truncated"},
    {.value = "sha512_256", .meaning = "SHA-2, 512/256 truncated"},
});

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
        .alias = "-z",
        .display = "--archive[=none|roots|all], -z[+|++|-]",
        .group = "traversal",
        .header = "Traversal",
        .summary = "descend into archives: -z- none, -z roots only, -z+ / bare --archive all",
        .details = "Treats each archive (tar, gz, bzip2, xz, zstd, lz4, zip, ...) as a directory, so a member is an "
                   "ordinary entry at a member path like `foo.tar.gz!inner/x` and the expression matches it with "
                   "the same -name / -type / -size / -newer every other entry gets - and the predicates and "
                   "fields that READ an entry (-grep, -content, -hash, {hash}, {lines}) read the member out of "
                   "its container. "
                   "The three modes are nested: none keeps find's behavior (an archive is one "
                   "plain file); roots dives only when a search root is itself an archive (pointing xff AT an "
                   "archive implies looking inside); all also dives archives discovered during the walk. Bare "
                   "--archive means all, and the short form carries chmod-style suffix signs (-z- none, -z roots, "
                   "-z+ all). The find style defaults to none, every xff-family style to roots. Members are "
                   "read-only, so -delete and the exec family refuse them rather than silently skipping. "
                   "Under `all`, a file met mid-walk is offered to the reader only if its NAME looks like "
                   "a container (see --archive-any); one named on the command line always is. A "
                   "build-time extra: the stock binary is lean and omits it (rebuild with --//xff:xff_archive); "
                   "asking for archive handling without it is a hard error.",
        .values = kArchiveValues,
        .topic = "archive",
        .extra = "archive",
    },
    {
        .name = "--archive-depth",
        .display = "--archive-depth=N",
        .group = "traversal",
        .header = "Traversal",
        .summary = "how many containers deep --archive dives (default 1)",
        .details = "Counted in CONTAINERS, not directory levels: the default 1 opens an archive but leaves an "
                   "archive INSIDE it a plain member, so a `.gem` shows its `data.tar.gz` without unpacking it. "
                   "`--archive-depth=2` opens that one too. Its own knob rather than part of -maxdepth because "
                   "nesting is where a decompression bomb lives - a few kilobytes can promise gigabytes per level "
                   "- while -maxdepth keeps counting member levels as the ordinary depth they are. Only `all` "
                   "nests: under `roots` a member is never a search root, so nothing inside the container is "
                   "dived whatever the value. N must be at least 1; use --archive=none / -z- to stop diving.",
        .affects = "--archive",
        .topic = "archive",
        .extra = "archive",
    },
    {
        .name = "--archive-aggregate",
        .display = "--archive-aggregate=<MODE>",
        .group = "traversal",
        .header = "Traversal",
        .summary = "what --summary / --histogram count when the walk dives (default members)",
        .details = "Diving makes one byte visible twice - once as the container's own size, once as its "
                   "members' - so a total that adds both describes no filesystem that exists. `members` (the "
                   "default) counts a dived container's members instead of the container, which is what "
                   "unpacking it and measuring the result would give; `container` counts the archives and "
                   "never what is in them, which is what the disk holds; `both` counts everything, the "
                   "archive AND its unpacked copy, for when the doubling is the point. Only the REDUCTIONS "
                   "are affected: -print and every action still see every entry the walk visits, so a member "
                   "is listed under `container` and the container is listed under `members`. `members` needs "
                   "the walk to open a container before deciding, so a `-prune` on a container no longer "
                   "avoids opening it - use another mode, or no reduction, to keep that.",
        .values = kArchiveAggregateValues,
        .affects = "--archive",
        .topic = "archive",
        .extra = "archive",
    },
    {
        .name = "--archive-delete",
        .display = "--archive-delete",
        .group = "traversal",
        .header = "Traversal",
        .summary = "let -delete remove an archive member, rewriting its container",
        .details = "There is no such thing as removing a member in place: an archive is a stream of "
                   "header and data records, so the container is written again from the members that "
                   "survive. That is why this is opt-in and why `-delete` refuses a member without it "
                   "- an action that silently rewrites a whole archive is not one to do by default. "
                   "The rewrite happens after the walk, once per container however many of its "
                   "members matched, because the walk is reading that same container while it runs. "
                   "The new archive keeps the original's format and compression (a `.tar.gz` stays a "
                   "gzipped tar) and every surviving member keeps its name, mode, times and content; "
                   "it is written beside the original and renamed over it only when complete, so an "
                   "interrupted run leaves the container as it was. `--dry-run` lists the members "
                   "that would go and writes nothing. A NATIVE phar is rewritten too, by xff's own "
                   "writer: the manifest and data section are rebuilt from the surviving entries "
                   "verbatim (so per-member gz / bz2 compression is untouched) and the trailing "
                   "signature is recomputed (md5 / sha1 / sha256 / sha512). Refused, with the reason "
                   "named: a format this build reads but cannot write (7-Zip, RAR, ISO); a TAR-based "
                   "or ZIP-based phar, whose signature is a MEMBER computed over the rest of the "
                   "container, so a rewrite would leave it stale and PHP would reject the result; an "
                   "OpenSSL-signed phar, which cannot be re-signed without its private key; a "
                   "compressed single file, which has no member list to rewrite; and a member of a "
                   "container nested inside another one.",
        .affects = "--archive",
        .topic = "archive",
        .extra = "archive",
    },
    {
        .name = "--archive-extract",
        .display = "--archive-extract",
        .group = "traversal",
        .header = "Traversal",
        .summary = "let -exec / -ok run on an archive member, via a temporary copy",
        .details = "A member is bytes inside a container, so there is no path a child process can "
                   "open and the exec family refuses one by default. With this flag the member is "
                   "written to its own temporary directory under the same name it has inside the "
                   "archive, and the child is handed THAT path: `{}` renders as the temporary file, "
                   "-execdir runs in the temporary directory, and -ok shows the copy in its prompt "
                   "before anything runs. Each copy is removed as soon as its child finishes (for a "
                   "`+` batch or a -j child, when the run ends), so nothing is left behind. It is "
                   "The copy goes to a MEMORY-BACKED directory where the platform has one "
                   "($XDG_RUNTIME_DIR or /dev/shm on Linux, both tmpfs), so a member never reaches a disk "
                   "and the child still gets an ordinary path; a member too large for the space that "
                   "directory reports free lands in the temporary directory instead, since a tmpfs is RAM "
                   "shared with the whole machine. It is "
                   "opt-in because the child is editing a COPY: a formatter or a patch tool will "
                   "report success and change nothing in the archive. -delete stays refused whatever "
                   "this flag says - removing a temporary copy would be a no-op dressed as a "
                   "deletion. The container itself is an ordinary file, so an action on IT never "
                   "needed this.",
        .affects = "--archive",
        .topic = "archive",
        .extra = "archive",
    },
    {
        .name = "--archive-write",
        .display = "--archive-write, -z++",
        .group = "traversal",
        .header = "Traversal",
        .summary = "arm both archive write flags (--archive-extract + --archive-delete)",
        .details = "One spelling for \"let actions touch members\", because the two write flags are almost "
                   "always wanted together: --archive-extract so -exec / -ok can run over a member, and "
                   "--archive-delete so -delete can remove one. It is exactly those two flags and nothing "
                   "else - the dive MODE is untouched, so pair it with --archive=all / -z+ when you also "
                   "want containers met mid-walk. The short `-z++` continues the -z sign ladder (-z- none, "
                   "-z roots, -z+ all) with \"all, and writable\", and it is the only short form: `-z*` was "
                   "considered and rejected, because a bare `-z*` errors in zsh (unmatched glob) and in "
                   "default bash silently expands if any file happens to match. Nothing here bypasses a "
                   "refusal: a container xff cannot rewrite "
                   "and a member no child can be handed still say so, and --dry-run still previews.",
        .affects = "--archive-delete,--archive-extract",
        .topic = "archive",
        .extra = "archive",
    },
    {
        .name = "--archive-any",
        .display = "--archive-any",
        .group = "traversal",
        .header = "Traversal",
        .summary = "under --archive=all, offer EVERY file to the reader, not only likely names",
        .details = "By default `all` only opens a file the walk met whose NAME looks like a container "
                   "(`.tar`, `.tgz`, `.zip`, `.jar`, `.phar`, ... - the reader's formats plus the "
                   "packages that are one of them underneath). Without that gate, walking a source tree "
                   "would open and format-bid every `.cc` and every binary in it, so the cost of diving "
                   "would fall on runs that dive nothing. The name is only a heuristic, and this flag is "
                   "the way out of it: an archive called `blob` or `backup.dat` is found with "
                   "--archive-any and missed without. It costs a read of every candidate file, which is "
                   "why it is not the default. A file NAMED on the command line is always opened - "
                   "pointing xff at it is the request - so this flag changes nothing for `--archive=roots`.",
        .affects = "--archive",
        .topic = "archive",
        .extra = "archive",
    },
    {
        .name = "--archive-separator",
        .display = "--archive-separator=STRING",
        .group = "traversal",
        .header = "Traversal",
        .summary = "string between container and member in a member path (default `!`)",
        .details = "A member path is `<container><separator><member>`, and there is no single ecosystem "
                   "convention - `!` (JAR / Java URLs), `#` (fragment style), and the multi-character `!/` or "
                   "`#/` other tools print all exist - so this is a presentation choice rather than something "
                   "hard-coded. ANY string is accepted, not a fixed menu, so xff can emit what another system "
                   "accepts. Rendering is plain concatenation and xff adds or removes no slash, so a member "
                   "stored with a leading slash keeps it: `a.tgz!/rooted` (and with `--archive-separator=!/`, "
                   "the doubled `a.tgz!//rooted`, which is why plain `!` is the better default). Parsing splits "
                   "at the FIRST occurrence and takes the remainder verbatim, so a path xff printed round-trips. "
                   "A plain `/` is allowed and composes with globs, but is lossy - a real directory named x.tar "
                   "becomes indistinguishable from an archive - so it is never the default.",
        .topic = "archive",
        .extra = "archive",
    },
    {
        .name = "--archive-prefix",
        .display = "--archive-prefix=[URI|STRING]",
        .group = "traversal",
        .header = "Traversal",
        .summary = "prefix a member path: empty (default), URI, or any literal string",
        .details = "Empty (the default) prints the bare path, `a.tgz!inner/x`. `URI` renders a well-formed "
                   "URI for handing to tools that read archive URLs: `archive:///abs/a.tar!x` for an absolute "
                   "container (empty authority then the path, as `file:///...` does) and the opaque "
                   "`archive:a.tgz!x` for a relative one - `archive://a.tgz` would be WRONG, since `//` starts "
                   "the authority and would make `a.tgz` a host name. Any other value is used LITERALLY (e.g. "
                   "`--archive-prefix=vfs:`), the same freedom --archive-separator has; `URI` is the one keyword, "
                   "spelled in caps like RE2 / PCRE2 / GLOB. There is deliberately no `none` value: it would be "
                   "indistinguishable from a literal prefix spelled `none`, which is why empty means no prefix. "
                   "Applies to PARSING too - under a prefix, a bare path is not accepted as a member path, so the "
                   "spellings never silently interchange. Whether the scheme should be per-format (`tar:` / "
                   "`zip:` / PHP's `phar:`) rather than the generic `archive:` is still open (see TODO.md).",
        .values = kArchivePrefixValues,
        .topic = "archive",
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
        .display = "--case=<MODE>, -i, -s[+|-]",
        .group = "matching",
        .header = "Matching",
        .summary = "letter case for matchers: -i insensitive, -s/-s+ smart, -s- sensitive (rg -> smart)",
        .details = "Controls case for -name/-path/-regex and the content matchers. sensitive matches exactly; "
                   "insensitive (-i) folds case; smart (-s / -s+) folds only when the pattern is all lower case and "
                   "matches exactly otherwise; -s- forces sensitive. rg defaults to smart.",
        .values = kCaseValues,
    },
    {
        .name = "--regextype",
        .display = "--regextype=<GRAMMAR>",
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
        .values = kRegextypeValues,
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
        .name = "--ignore-vcs",
        .display = "--ignore-vcs",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "respect version-control ignore files (.gitignore / .git/info/exclude / core.excludesFile)",
        .details = "The rg-style affirmative for the VCS ignore-file layer - today git's (.gitignore at any depth, "
                   ".git/info/exclude, core.excludesFile), the same layer -g / --gitignore auto enables. Use it to "
                   "countermand an earlier --no-ignore-vcs or a style default. Independent of --ignore-files "
                   "(.ignore / .xffignore), which keep their own switch; --no-ignore / -u still turns off every "
                   "ignore source. Last of the ignore-mode flags wins.",
    },
    {
        .name = "--no-ignore-vcs",
        .display = "--no-ignore-vcs",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "do not respect version-control ignore files (keeps .ignore / .xffignore)",
        .details = "Drops the VCS ignore-file layer (git's .gitignore / .git/info/exclude / core.excludesFile) while "
                   "leaving --ignore-files (.ignore / .xffignore) untouched - that is the difference from "
                   "--no-ignore / -u, which turns off every ignore source. Today git is the only VCS ignore file xff "
                   "reads, so this is nearly --gitignore=off. Last of the ignore-mode flags wins.",
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
        .name = "--skip-vcs",
        .display = "--skip-vcs[=<LIST>]",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "prune VCS metadata dirs (.git, .hg, ...); bare/=all = every known VCS, =LIST a subset",
        .details = "Prunes version-control metadata directories at any depth (like ripgrep / fd), so a search "
                   "never wades into repo plumbing. Bare --skip-vcs (or =all) covers every known VCS: git (.git), "
                   "hg (.hg), svn (.svn), jj (.jj), bzr (.bzr), darcs (_darcs), cvs (CVS). A comma list "
                   "(--skip-vcs=git,hg) is an explicit, frozen subset - it never changes if a VCS is added to the "
                   "default set later. --no-skip-vcs (or =none) turns it off. Independent of --hidden, so the "
                   "user's own dotfiles (.bazelrc, .gitignore) still show. -g / gitignore mode implies "
                   "--skip-vcs=git (only .git); an explicit --skip-vcs overrides that. Default off otherwise.",
        .values = kSkipVcsValues,
    },
    {
        .name = "--no-skip-vcs",
        .display = "--no-skip-vcs",
        .group = "filter",
        .header = "Filter & Ignore",
        .summary = "keep VCS metadata dirs in the walk (opts out of --skip-vcs and the -g .git default)",
    },
    {
        .name = "--format",
        .display = "--format=<FORMAT>",
        .group = "output",
        .header = "Output",
        .summary = "output format: plain, nul, jsonl, csv, tsv, aligned, markdown (md), tree; default plain",
        .values = kFormatValues,
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
        .display = "--hash-algorithm=<ALGO>",
        .group = "output",
        .header = "Output",
        .summary = "default digest for -hash / {hash} (sha256 default; md5, sha512, blake3, and more)",
        .details = "Sets the default digest algorithm for the -hash action and the {hash} field. sha256 is the "
                   "default; a `-hash=ALGO` spec or a `{hash:ALGO}` qualifier overrides it per use.",
        .values = kHashAlgorithmValues,
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
        .display = "--summary[=<GROUP>]",
        .group = "output",
        .header = "Output",
        .summary = "aligned count + size table (or --format=jsonl rows) instead of each match; repeatable",
        .details = "Replaces the per-match listing with an aggregate table: match count and total size per group "
                   "(overall, by type, extension, programming language, media (MIME) type, user (owner), owning "
                   "group, or file digest). The categorical keys reuse the {mime}/{user}/{group}/{hash} field "
                   "vocabulary; --summary=hash groups identical files into one bucket (a dedup count, reading every "
                   "file). A "
                   "{template} key groups by any field value (e.g. --summary='{ext}-{type}'); a single m// "
                   "extraction key (--summary='{capture.NAME:m/re/\\1/}') groups per extracted line, so a "
                   "per-file command's multi-line output tallies per key (e.g. git-blame lines per author) - the "
                   "size column is not meaningful there. Repeatable: each --summary is its own table (e.g. "
                   "--summary=ext --summary=type), printed in order. --top=N limits the rows of each, "
                   "--summary-precision sets the scaled-size digits, and --format=jsonl emits one object per group "
                   "for scripts.",
        .values = kSummaryValues,
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
        .name = "--shards",
        .display = "--shards[=auto|SCHEME,...]",
        .group = "output",
        .header = "Output",
        .summary = "collapse each set of sharded files (e.g. data-00000-of-00010) to one line",
        .details = "Recognizes sharded-file naming conventions and collapses each logical set to a single line "
                   "instead of listing every shard. Bare --shards (or =auto) enables all built-in schemes: "
                   "`<stem>-<index>-of-<total>` (of), `<stem>.<NNN>` (dotnum), and `<stem>_<NNN>` (underscore). "
                   "Restrict to specific schemes with a comma list, e.g. --shards=of,dotnum. Grouping is "
                   "per-directory; files that match no scheme are listed unchanged. Off by default.",
        .values = kShardsValues,
        .topic = "stats",
    },
    {
        .name = "--shards-show",
        .display = "--shards-show=first|wildcard|count",
        .group = "output",
        .header = "Output",
        .summary = "how a collapsed shard set's line reads (default first)",
        .details = "Picks each collapsed set's display: first = the representative (lowest-index) shard's path; "
                   "wildcard = the masked-index name (the index digits shown as `???`); count = the wildcard "
                   "plus the shard count. An incomplete set is always annotated `(present/expected - "
                   "INCOMPLETE)`. Only meaningful with --shards.",
        .values = kShardsShowValues,
        .topic = "stats",
    },
    {
        .name = "--shards-dedup",
        .display = "--shards-dedup=first|mtime|error",
        .group = "output",
        .header = "Output",
        .summary = "how same-index shard duplicates are resolved (default first)",
        .details = "When two files are the same logical shard (they differ only by an opaque tail, e.g. a "
                   "regeneration id), --shards-dedup picks which is the representative: first keeps the "
                   "lexicographically-first name; mtime keeps the newest; error treats the duplicate as an "
                   "error and fails the run (non-zero exit). Only meaningful with --shards.",
        .values = kShardsDedupValues,
        .topic = "stats",
    },
    {
        .name = "--shard-pattern",
        .display = "--shard-pattern=REGEX",
        .group = "output",
        .header = "Output",
        .summary = "a custom shard scheme via a named-capture regex (repeatable); the escape hatch",
        .details = "Defines a custom sharded-file scheme for --shards when the built-ins do not fit. REGEX is "
                   "an RE2 pattern with named groups: `(?P<stem>...)` and `(?P<index>...)` are required, "
                   "`(?P<total>...)` and `(?P<dup>...)` are optional. Repeatable; the patterns are tried in "
                   "order, before the built-in schemes. Only meaningful with --shards.",
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
        .summary = "-grep context lines: N both sides, or A:N,B:N,C:N for after/before/both",
        .details = "`--context=2` is grep's `-C 2` (two lines either side); the A / B / C keys inside the "
                   "value select one side (`--context=A:3,B:1`), which is what `--after-context` and "
                   "`--before-context` spell one at a time. xff has NO single-dash `-A` / `-B` / `-C`: those "
                   "letters are unclaimed for now (see TODO.md), and a single-dash flag would be an "
                   "expression primary under xff's dash-count rule rather than a whole-run option.",
        .affects = "-grep,-diff,--diff-context",
    },
    {
        .name = "--after-context",
        .display = "--after-context=N",
        .group = "output",
        .header = "Output",
        .summary = "with -grep, print N lines of context after each match (= --context=A:N)",
        .affects = "-grep",
    },
    {
        .name = "--before-context",
        .display = "--before-context=N",
        .group = "output",
        .header = "Output",
        .summary = "with -grep, print N lines of context before each match (= --context=B:N)",
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
        .name = "--color-scheme",
        .display = "--color-scheme=<SCHEME>",
        .group = "output",
        .header = "Output",
        .summary = "which palette colour comes from: the terminal's ls theme, or xff's own",
        .details = "Colour is a whole-run choice, so this one palette is used by every surface that "
                   "colours - the plain listing and -ls alike; they cannot disagree. $LS_COLORS is the "
                   "variable `ls` and `dircolors` use, and xff reads the same keys: the two-letter "
                   "types (di, ln, ex, pi, so, bd, cd, fi) and the per-extension `*.tar=` entries. "
                   "Where only BSD's $LSCOLORS is set - the macOS case - that is read instead: its 11 "
                   "letter pairs carry the same types in a fixed order, with no way to say \"leave "
                   "this plain\" and no per-extension entries, so `merged` is the interesting scheme "
                   "there. $LS_COLORS wins when both are set, being the richer format. Both variables "
                   "are read on every platform rather than one per OS: which one is SET is better "
                   "evidence than which system this is (a macOS shell with GNU coreutils is themed "
                   "through $LS_COLORS, and $LSCOLORS is not macOS-only), and the fixed 22-character "
                   "shape makes the BSD one self-validating. "
                   "\"Use ls's colours\" turns out to mean three different things, so each has its "
                   "own name, spelled the way logic spells it: `+` is OR, and the merge is AND. "
                   "`auto` (the default, also `ls+xff` or `ls-or-xff`) is the theme OR xff's scheme - "
                   "a theme that is set at all is the whole answer, and with none set xff's scheme is, "
                   "so the decision is per VARIABLE; `default` is a fourth spelling of it, for a "
                   "config file that wants whatever the default currently is. `ls` is the theme "
                   "ALONE, so a type it never mentions prints uncoloured exactly as in a real ls "
                   "listing (and with no theme set, nothing is coloured). `merged` (also "
                   "`ls-and-xff`) is the theme "
                   "AND xff's scheme, merged per KEY: the theme where it speaks, xff's colour for "
                   "every key it omits - for a sparse theme you want filled in. (`ls&xff` is "
                   "deliberately not accepted: an unquoted `&` backgrounds the command.) `xff` "
                   "ignores $LS_COLORS entirely. An EMPTY value in the theme (`di=`) is it "
                   "saying \"leave these plain\" and is honoured as such; a malformed entry is "
                   "skipped rather than failing the run, as in ls. Whether colour is emitted at all "
                   "is --color's business, not this flag's.",
        .values = kColorSchemeValues,
        .affects = "--color",
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
        .name = "--width",
        .display = "--width[=auto|none|COLS]",
        .group = "output",
        .header = "Output",
        .summary = "wrap column for plain --help text: auto (terminal width, else unwrapped), none, or a count",
        .details = "Wraps the flowing text of --help and --help=TOPIC (option and topic descriptions) to a "
                   "column width. auto uses the terminal width when stdout is a terminal (honoring $COLUMNS), "
                   "and leaves output unwrapped when it is not (a pipe or file); none (or 0) disables wrapping; "
                   "a positive integer sets a fixed width. Aligned vocabulary tables and example blocks keep "
                   "their own layout. Does not affect the file listing, --man, or --markdown.",
    },
    {
        .name = "--pager",
        .display = "--pager[=auto|always|all|never]",
        .group = "output",
        .header = "Output",
        .summary = "page output: auto (help / man / markdown on a tty), all (plus the listing), always, never",
        .details = "Pages the long meta output (--help, --help=TOPIC, --man, --markdown) through a pager. auto "
                   "pages only when stdout is a terminal; always pages even through a pipe; never (or --no-pager) "
                   "disables it. The pager command is $XFF_PAGER, else $PAGER, else `less -FRX`; set either "
                   "variable to empty to disable. all additionally pages the FILE LISTING: the pager is started "
                   "once and the whole walk streams into it, so the first screen appears while the walk is still "
                   "running and quitting it ends the run quietly. Unlike always, all stays terminal-only - a "
                   "listing forced through a pager in a pipeline would feed the pager's screen handling to the "
                   "next command. It also steps aside for an expression that needs the terminal itself (-ok, "
                   "-okdir, -exec, -execdir, which can hand the terminal to an editor) and for --quiet, which "
                   "prints nothing to page; those runs are simply unpaged.",
        .values = kPagerValues,
    },
    {
        .name = "--no-pager",
        .display = "--no-pager",
        .group = "output",
        .header = "Output",
        .summary = "never page the help / man / markdown output (an alias for --pager=never)",
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
    {
        .name = "--time-zone-suffix",
        .display = "--time-zone-suffix[=auto|always|never]",
        .group = "time",
        .header = "Time",
        .summary = "show the zone offset on a time field: auto (per format), always, or never",
        .details = "Controls whether a time field's named preset renders its trailing zone (+0100, +01:00). "
                   "auto keeps each preset's default (space / iso / rfc3339 show it, asctime / epoch omit it); "
                   "never drops it; always forces it, even on a preset that omits one. Accepts true / yes / on "
                   "(= always) and false / no / off (= never). The inherently-zoned zulu / zulu-dense / asn1z "
                   "always keep their mandatory Z, and a custom strftime --time-format is never altered - control "
                   "its zone with %z / %Ez / %Z yourself. asn1's zone is optional: always adds its ASN.1-style "
                   "offset (+0100, no separator), never / auto leave it bare.",
        .values = kZoneSuffixValues,
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
  // gitignore forms (= --gitignore=on/off), the short case forms -i (insensitive),
  // -s/-s+ (smart), -s- (sensitive) (= --case=...), and the -z+/-z- short archive forms
  // (= --archive=all/none; bare -z is a table row via the alias).
  if (arg == "-0" || arg == "-g+" || arg == "-g-" || arg == "-i" || arg == "-s" || arg == "-s+" || arg == "-s-"
      || arg == "-z+" || arg == "-z++" || arg == "-z-") {
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
    return flag != nullptr && absl::StrContains(flag->display, '=');
  }
  return false;
}

bool ExtraEnabled(std::string_view key) {
  // Answered by the LINKER, not by a parallel define: an extra registers itself into its backend slot
  // at static init, so asking the slot cannot drift from what the binary actually contains (the same
  // question `Pcre2Available()` answers for the regex slot). In the lean default build nothing
  // registers and every extra reads as off. New extras add a branch here and in ExtraBuildFlag.
  if (key == "archive") {
    return archive::ContainerSupportAvailable();
  }
  return false;  // unknown / not-yet-wired extra
}

std::string_view ExtraBuildFlag(std::string_view key) {
  // The label a user must actually pass to rebuild with the extra. Spelled out per extra rather
  // than derived from the key: the Bazel flags carry an `xff_` prefix that the human-facing key
  // does not (`archive` -> `//xff:xff_archive`), and pcre2's flag is `xff_pcre`, so any derivation
  // rule would print a flag that does not exist - which is worse than useless in an error message.
  if (key == "archive") {
    return "--//xff:xff_archive";
  }
  if (key == "pcre2") {
    return "--//xff:xff_pcre";
  }
  return {};  // unknown extra: the caller omits the rebuild hint rather than inventing a flag
}

}  // namespace xff::cli
