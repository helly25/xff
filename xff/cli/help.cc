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

#include "xff/cli/help.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "xff/cli/globals.h"
#include "xff/fields/fields.h"
#include "xff/license/license.h"
#include "xff/registry/descriptor.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

std::string_view KindLabel(registry::Kind kind) {
  switch (kind) {
    case registry::Kind::kAction: return "action";
    case registry::Kind::kOperator: return "operator";
    case registry::Kind::kTest: return "test";
  }
  return "";
}

// "(test, find)" / "(action, xff, runs commands)".
std::string Tags(const registry::Descriptor& descriptor) {
  std::vector<std::string_view> tags;
  tags.push_back(KindLabel(descriptor.kind));
  tags.push_back(descriptor.style == registry::Style::kXff ? "xff" : "find");
  if (descriptor.safety == registry::Safety::kSecurity) {
    tags.emplace_back("runs commands");
  } else if (descriptor.safety == registry::Safety::kSafety) {
    tags.emplace_back("modifies the filesystem");
  }
  return absl::StrCat("(", absl::StrJoin(tags, ", "), ")");
}

// Appends an indented influence sub-block ("Header:\n" then one entry per line) to `out` when
// `items` is non-empty. The entries sit two spaces deeper than the header so the reader stays
// anchored under it (per the help design). Shared by the derived "Affects:" / "Affected by:"
// cross-references on both globals and primaries.
void AppendInfluenceBlock(std::string* out, std::string_view header, const std::vector<std::string_view>& items) {
  if (items.empty()) {
    return;
  }
  absl::StrAppend(out, "    ", header, "\n");
  for (const std::string_view item : items) {
    absl::StrAppend(out, "      ", item, "\n");
  }
}

// The forward affects list of `flag` (GlobalFlag.affects, comma-split, empty tokens skipped).
std::vector<std::string_view> AffectsList(const GlobalFlag& flag) {
  return absl::StrSplit(flag.affects, ',', absl::SkipEmpty());
}

// The reverse of AffectsList: every global flag that declares (via its affects list) that it
// changes `name`'s behavior, in Globals() display order. `name` is a primary ("-diff") or a
// global ("--context"). Deriving both directions from the one affects field keeps an entry's
// "Affected by:" block in lock-step with the flags' forward declarations (globals_test guards it).
std::vector<std::string_view> AffectedBy(std::string_view name) {
  std::vector<std::string_view> out;
  for (const GlobalFlag& flag : Globals()) {
    for (const std::string_view token : absl::StrSplit(flag.affects, ',', absl::SkipEmpty())) {
      if (token == name) {
        out.push_back(flag.name);
        break;
      }
    }
  }
  return out;
}

std::string RenderOne(const registry::Descriptor& descriptor, bool with_details = false) {
  std::string out =
      absl::StrCat(descriptor.name, ArgHint(descriptor), "  ", Tags(descriptor), "\n    ", descriptor.summary, "\n");
  if (with_details) {
    if (!descriptor.details.empty()) {
      absl::StrAppend(&out, "    ", descriptor.details, "\n");  // the long explanation (--help=NAME / --help=full)
    }
    AppendInfluenceBlock(&out, "Affected by:", AffectedBy(descriptor.name));  // derived from the globals
  }
  return out;
}

std::string RenderGlobalFlag(const GlobalFlag& flag, bool with_details = false) {
  std::string out = absl::StrCat(flag.display, "  (global, ", flag.xff ? "xff" : "find", ")\n    ", flag.summary, "\n");
  if (!flag.extra.empty() && !ExtraEnabled(flag.extra)) {
    absl::StrAppend(
        &out, "    NOT built into this binary: rebuild with --//xff:", flag.extra,
        " (used as-is, it is a hard error).\n");
  }
  if (with_details) {
    if (!flag.details.empty()) {
      absl::StrAppend(&out, "    ", flag.details, "\n");  // the long explanation (--help=NAME / --help=full)
    }
    AppendInfluenceBlock(&out, "Affects:", AffectsList(flag));          // forward: what this flag changes
    AppendInfluenceBlock(&out, "Affected by:", AffectedBy(flag.name));  // reverse: flags that change it
  }
  return out;
}

// Appends the expression vocabulary to `out`, grouped by kind (Tests, Actions,
// Operators), one `name  summary` line per primary. Shared by the full index and
// the expressions-only listing so both stay in lock-step with registry::All().
void AppendExpressions(std::string* out) {
  struct Section {
    registry::Kind kind;
    std::string_view title;
  };

  for (const Section& section :
       {Section{registry::Kind::kTest, "Tests"}, Section{registry::Kind::kAction, "Actions"},
        Section{registry::Kind::kOperator, "Operators"}}) {
    absl::StrAppend(out, "\n", section.title, ":\n");
    for (const registry::Descriptor& descriptor : registry::All()) {
      if (descriptor.kind == section.kind) {
        absl::StrAppendFormat(out, "  %-24s%s\n", descriptor.name, descriptor.summary);
      }
    }
  }
}

std::string RenderIndex() {
  std::string out =
      "xff vocabulary. Use `--help=NAME` for one entry (e.g. `--help=-regex`, `--help=--sort`), "
      "or `--help` for the usage overview.\n";

  // Whole-run global options, grouped by GlobalFlag.group (shared with the usage page).
  absl::StrAppend(&out, RenderOptions(""));

  AppendExpressions(&out);  // Tests / Actions / Operators, grouped by kind
  absl::StrAppend(&out, "\nHelp topics (--help=TOPIC):\n", RenderTopicIndex("  "));
  return out;
}

// The expression vocabulary alone (Tests / Actions / Operators with one-line
// summaries), for `--help=expressions` -- the full annotated list the usage page's
// grouped overview points at, without the whole-run global flags.
std::string RenderExpressions() {
  std::string out =
      "xff expression vocabulary (tests, operators, actions applied to each entry). "
      "Use `--help=NAME` for one entry; `--help` for the usage overview.\n";
  AppendExpressions(&out);
  return out;
}

// The `--help=fields` topic: the {field} placeholder vocabulary. (Not `--help=format`,
// which is the --format record-format flag -- a different concept.) The named-field rows
// come from fields::FieldDocs() (the SOT, covered by
// a fields_test drift guard) grouped by heading; the dynamic namespaces, qualifiers,
// and brace rules are prose, and the % directives cross-reference `--help=-printf`.
std::string RenderFields() {
  std::string out =
      "xff {field} placeholder vocabulary. Substituted per entry in --template and --format, "
      "in -printf via the %{field} escape, and (with --exec-fields) in -exec.\n";

  std::string_view group;
  for (const fields::FieldDoc& doc : fields::FieldDocs()) {
    if (doc.group != group) {
      group = doc.group;
      absl::StrAppend(&out, "\n", doc.header, ":\n");  // key drives grouping; header is the display
    }
    std::string names = absl::StrCat("{", doc.name, "}");
    for (const std::string_view alias : doc.aliases) {
      absl::StrAppend(&names, " {", alias, "}");
    }
    absl::StrAppendFormat(&out, "  %-18s%s\n", names, doc.summary);
  }

  absl::StrAppend(
      &out,
      "\nBraces:\n"
      "  {{ and }} emit literal braces; {} is an alias for {path}; an unknown field renders\n"
      "  empty; a malformed or unterminated { stays literal.\n"
      "\nDynamic namespaces:\n"
      "  {0}..{N}          -regex captures ({0} the whole match, {1}..{N} the groups)\n"
      "  {env.NAME}        a process environment variable\n"
      "  {def.NAME}        a --define value\n"
      "  {capture.NAME}    a -capture command result\n"
      "\nQualifiers ({field:QUAL}):\n"
      "  {mtime:FMT}       time format: strftime (%Y-%m-%d) or preset (iso, epoch); see "
      "--time-format / --timezone\n"
      "  {size:h}          human-readable size\n"
      "  {name:s/RE/R/f}   RE2 rewrite of the value (flags g=all, i=ignore-case; any delimiter)\n"
      "  {cap:m/RE/R/f}    per-line extraction: a value stream, e.g. a --summary key (m//, s///'s "
      "list-producing sibling)\n"
      "  {cap:m/RE/R/;join(SEP)}\n"
      "                    reduce the stream to one scalar (join, SEP default newline) so m// is usable "
      "in a scalar\n"
      "                    context (-printf / --template / -exec); reducers are function-notation, "
      "e.g. join(, )\n");
  absl::StrAppendFormat(
      &out, "  %-18s%s%s\n", "{path:COMP}",
      "path component of the value: ", absl::StrJoin(fields::PathComponentKeywords(), "|"));
  absl::StrAppend(
      &out,
      "                    any path-valued field composes, e.g. {relpath:stem}, {def.B:dir}\n"
      "\nFor -printf's own % directives (%p %f %s %t ...) and the %{field} escape that bridges\n"
      "them to this vocabulary, see `--help=-printf`.\n");
  return out;
}

// The `--help=stats` topic: the two terminal reductions, --summary and --histogram. Both replace
// the per-match listing with an aggregate over the whole walk; they are independent and combinable.
std::string RenderStats() {
  // The flags (and the bucket/measure grammar, which lives in --histogram's details) are pulled
  // from the globals SOT via the "stats" topic tag, so this body cannot drift from the flags.
  std::string out =
      "xff statistics reductions. --summary and --histogram replace the per-match listing with an\n"
      "aggregate over all matches; they are independent and combinable (one walk feeds both), and\n"
      "an explicit action (-print / -exec) still runs. --format=jsonl emits machine rows instead.\n"
      "\n";
  for (const GlobalFlag& flag : Globals()) {
    if (flag.topic == "stats") {
      absl::StrAppend(&out, RenderGlobalFlag(flag, /*with_details=*/true), "\n");
    }
  }
  absl::StrAppend(
      &out,
      "Examples:\n"
      "  xff --summary=ext                                   files + total size per extension\n"
      "  xff --histogram=ext                                 a bar chart of files per extension\n"
      "  xff --histogram='ext:sum(lines)'                    total lines per extension\n"
      "  xff --histogram=size                                the file-size distribution\n"
      "  xff --summary=type --histogram=ext --format=jsonl   both, as machine rows\n");
  return out;
}

// The `--help=config` topic: how xff resolves options (layered tiers + the command line), how a
// style is chosen (--config and the argv[0] invocation name), and how dangerous --xffrc directives
// are armed. The config flags are pulled from the globals SOT via the "config" topic tag, so the
// flag list here cannot drift; the layering / argv[0] / arming rules are prose.
std::string RenderConfig() {
  std::string out =
      "xff configuration. Options resolve from layered config tiers, then the command line; later\n"
      "layers win. A style (find / xff / rg) sets the baseline defaults, which the tiers and the\n"
      "command line then adjust. Run --explain to print exactly what resolved.\n"
      "\n"
      "Layers, lowest to highest precedence:\n"
      "  system config   machine-wide defaults (+ a root-owned [policy] that can hard-deny arming)\n"
      "  user config     your personal defaults\n"
      "  --xffrc=FILE    an explicitly named file (repeatable) - a NON-ARMING tier\n"
      "  command line    flags and --config, highest\n"
      "\n"
      "There is no project / ancestor .xffrc discovery: config comes from the system and user files\n"
      "plus any --xffrc you name. --no-config ignores the discovered system/user files.\n"
      "\n"
      "Choosing a style:\n"
      "  --config=NAME selects find / xff / rg (repeatable, last wins); see --help=styles for the table.\n"
      "  The invocation name (argv[0]) is the leading selector, so a symlink named `find` runs the\n"
      "  strict find style and `rg` the rg style; any other name (e.g. a `mytool` symlink) activates a\n"
      "  same-named config block over the xff default. An explicit --config still stacks on top.\n"
      "\n"
      "Arming dangerous directives:\n"
      "  A dangerous directive (the exec family -exec/-execdir/-ok/-capture, or -delete) carried by an\n"
      "  --xffrc file is inert unless --allow-exec is set from a TRUSTED tier (the command line or the\n"
      "  system/user config, never an --xffrc file itself). Unarmed lines are dropped with a warning;\n"
      "  the root system [policy] can hard-deny even --allow-exec.\n"
      "\n"
      "Config flags:\n";
  for (const GlobalFlag& flag : Globals()) {
    if (flag.topic == "config") {
      absl::StrAppend(&out, RenderGlobalFlag(flag, /*with_details=*/true), "\n");
    }
  }
  return out;
}

// One cookbook recipe: a task, the command that does it, and a one-line note on how it works.
struct Recipe {
  std::string_view task;     // what the recipe accomplishes
  std::string_view command;  // the exact command (correct as written; users copy it)
  std::string_view note;     // how it works / which building blocks it composes
};

// The `--help=cookbook` topic (aliases examples / recipes), folded into --help=full: task-oriented
// worked examples that compose xff's building blocks end to end. The SOT is the recipe list below;
// every command is kept runnable as written. This complements the reference topics (--help=fields /
// --help=stats / --help=NAME), which describe pieces in isolation.
std::string RenderCookbook() {
  const std::vector<Recipe> recipes = {
      {.task = "Ten largest files",
       .command = "xff . -type f -printf '%s\\t%p\\n' | sort -rn | head",
       .note = "%s is the size, %p the path; the shell sorts and takes the top ten. -printf builds any "
               "columnar line you need."},
      {.task = "Disk use per file type",
       .command = "xff --summary=ext",
       .note = "a count + total size per extension; swap in --histogram=ext for bars, or "
               "--histogram='ext:sum(lines)' to rank by lines. See --help=stats."},
      {.task = "Delete stale temp files, safely",
       .command = "xff . -type f -name '*.tmp' -mtime +7 -delete --dry-run",
       .note = "lists what -delete WOULD remove (guarded by --dry-run); rerun without it to delete. "
               "-delete implies -depth so directories empty first."},
      {.task = "Search code content, filtered by language",
       .command = "xff src -lang 'C*' -grep 'TODO'",
       .note = "prints every TODO line as path:lineno:text in C / C++ / C# files; add -c for per-file "
               "counts or --context=2 for surrounding lines."},
      {.task = "Per-file git-blame author line counts",
       .command = "xff . -text -exec git blame --line-porcelain {} \\; "
                  "| grep '^author ' | sort | uniq -c | sort -rn",
       .note = "runs git blame on each text file; the shell pipe tallies lines per author across the "
               "tree. -text skips binaries (which git blame cannot line-blame). -exec feeds any pipeline "
               "the field vocabulary cannot express alone."},
      {.task = "Author line counts, natively (no shell pipe)",
       .command = "xff -g --summary='{capture.blame:m/^author (.+)$/\\1/}' . -text "
                  "-capturedir=blame git blame --line-porcelain {} \\;",
       .note = "the recipe above with the awk|sort tail folded into xff. -capturedir runs git blame in each "
               "file's own directory (repo-safe, works across nested repos); --summary folds that output via "
               "an m// extraction, tallying lines per author across the tree - no external pipe. -g honors "
               ".gitignore and skips .git; -text keeps blame off binaries. Pass several roots (a b c ...) to "
               "span multiple trees. Globals (--summary, -g) come before the first path."},
      {.task = "Checksum manifest for a tree",
       .command = "xff . -type f -hash=sha256",
       .note = "prints `DIGEST  PATH` per file (like sha256sum); redirect to a file to snapshot a tree, "
               "then diff two runs to spot changes."},
      {.task = "Recently changed files as machine rows",
       .command = "xff . -type f -mtime -1 --format=jsonl",
       .note = "everything modified in the last day, one JSON object per file, ready for jq or a script."},
  };
  std::string out =
      "xff cookbook: worked examples that compose xff's building blocks. Each shows a task, the\n"
      "command, and how it works. See --help=fields for {field}s and --help=stats for the reductions.\n";
  for (const Recipe& recipe : recipes) {
    absl::StrAppend(&out, "\n  ", recipe.task, "\n    ", recipe.command, "\n    ", recipe.note, "\n");
  }
  return out;
}

// The `--help=licenses` topic: a minimum-viable license summary. xff's own license, the core
// libraries always linked in, and the build extras with whether THIS binary has them (via
// ExtraEnabled, so it reflects the actual build). Full notice texts live in the NOTICE file; this
// is the at-a-glance answer to "what is in this binary, and under what licenses?".
// `--help=notice` (alias notices): the third-party component manifest, reproduced verbatim from the
// repo NOTICE compiled into the binary (see notices.h) so a single-file release is self-contained,
// prefixed with the one build-dependent line -- which extras THIS binary actually contains.
std::string RenderNotice() {
  std::string out = "Build extras compiled into this binary: ";
  absl::StrAppend(&out, ExtraEnabled("archive") ? "archive" : "none (lean build)", "\n\n");
  absl::StrAppend(&out, license::NoticeText());
  return out;
}

// `--help=license` (alias licenses): the copyright + grant (CopyrightNotice, so --help=license and
// --help=notice can never state a different owner) that COMPLETES the licensing statement per Apache
// 2.0's own APPENDIX, followed by xff's own license (Apache-2.0) reproduced verbatim from the repo
// LICENSE compiled into the binary. Separate from `--help=notice`, which is the third-party
// attribution; a user asking for either gets the full text, never a pointer to a file.
std::string RenderLicense() {
  // LicenseText() already opens with a blank line, so the copyright block and the license body are
  // separated by exactly one blank line with no extra separator here.
  return absl::StrCat(license::CopyrightNotice(), license::LicenseText());
}

// The `--help=help` topic: a guide to the (subcommand-free) help system, then the
// generated topic index. So there is one place a user can ask "how do I get help?".
std::string RenderHelpGuide() {
  std::string out =
      "xff help system. xff has no subcommands; every kind of help is a flag:\n"
      "\n"
      "  xff --help              this usage overview (also -h)\n"
      "  xff --help=NAME         full help for one option or primary (e.g. --help=-regex, --help=--sort)\n"
      "  xff --help=TOPIC        one of the topics below\n"
      "  xff --help=full         the full detailed reference (also --help-full / --help-long / --help-all)\n"
      "  xff --man               the man page (roff; pipe to `mandoc`, or `man -l -` on GNU/Linux)\n"
      "  xff --markdown          a Markdown reference of every option and primary\n"
      "\nTopics (--help=TOPIC):\n";
  absl::StrAppend(&out, RenderTopicIndex("  "));
  return out;
}

// The `--help=full` (aliases long / all) topic: the full detailed reference -- every
// whole-run option and every expression primary, each rendered like `--help=NAME`.
// Reuses RenderGlobalFlag / RenderOne so the detail cannot drift from per-entry help.
std::string RenderFull(bool detailed) {
  std::string out =
      detailed ? "xff full reference: every whole-run option and expression primary, with explanations. "
                 "See `xff --help` for the usage overview.\n\nOPTIONS\n"
               : "xff reference: every whole-run option and expression primary (summaries; --help=full adds the "
                 "explanations). See `xff --help` for the usage overview.\n\nOPTIONS\n";
  std::string_view group;
  for (const GlobalFlag& flag : Globals()) {
    if (flag.group != group) {
      group = flag.group;
      absl::StrAppend(&out, "\n", flag.header, ":\n");  // group is the key; header is the display
    }
    absl::StrAppend(&out, "  ", RenderGlobalFlag(flag, detailed));
  }
  absl::StrAppend(&out, "\nEXPRESSION\n");
  for (const auto& [kind, title] :
       {std::pair{registry::Kind::kTest, "Tests"}, std::pair{registry::Kind::kAction, "Actions"},
        std::pair{registry::Kind::kOperator, "Operators"}}) {
    absl::StrAppend(&out, "\n", title, ":\n");
    for (const registry::Descriptor& descriptor : registry::All()) {
      if (descriptor.kind == kind) {
        absl::StrAppend(&out, "  ", RenderOne(descriptor, detailed));
      }
    }
  }
  return out;
}

}  // namespace

// Read from the descriptor grammar (arity / binding) so the synopsis never drifts
// from the parser; shared with the man page. Documented in help.h.
std::string ArgHint(const registry::Descriptor& descriptor) {
  if (descriptor.binding == registry::Binding::kLabelRegex) {
    return "=NAME[=REGEX] CMD... ;";
  }
  if (descriptor.arity < 0) {
    return " CMD... ;";  // variadic until ';' (or '+' for -exec / -execdir)
  }
  std::string hint;
  for (int i = 0; i < descriptor.arity; ++i) {
    absl::StrAppend(&hint, " ARG");
  }
  return hint;
}

std::vector<HelpTopic> HelpTopics() {
  return {
      {.name = "help", .aliases = {}, .summary = "how the help system works, and the topics here"},
      {.name = "list", .aliases = {}, .summary = "index of every option and expression primary"},
      {.name = "all", .aliases = {}, .summary = "every option and primary, summaries only"},
      {.name = "expressions", .aliases = {}, .summary = "the expression vocabulary: tests, operators, actions"},
      {.name = "fields", .aliases = {}, .summary = "the {field} placeholder vocabulary", .in_full = true},
      {.name = "printf", .aliases = {}, .summary = "the -printf % directives and the %{field} escape", .in_full = true},
      {.name = "time", .aliases = {}, .summary = "time-format presets and strftime patterns", .in_full = true},
      {.name = "size", .aliases = {}, .summary = "-size units (c/w/b/k/M/G/T/P/E) and +/-", .in_full = true},
      {.name = "grammars",
       .aliases = {},
       .summary = "the --regextype grammars (RE2/EXACT/FNMATCH/GLOB/SHGLOB/PCRE2)",
       .in_full = true},
      {.name = "config", .aliases = {}, .summary = "config tiers, style selection (--config / argv[0]), and arming"},
      {.name = "styles", .aliases = {"flavors"}, .summary = "the find / xff / rg flavor comparison"},
      {.name = "extras",
       .aliases = {},
       .summary = "optional build extras (PCRE2, archive) and whether this binary has them"},
      {.name = "stats", .aliases = {}, .summary = "the --summary and --histogram reductions"},
      {.name = "cookbook",
       .aliases = {"examples", "recipes"},
       .summary = "worked examples that compose xff end to end",
       .in_full = true},
      {.name = "notice", .aliases = {"notices"}, .summary = "third-party components + what this binary contains"},
      {.name = "license", .aliases = {"licenses"}, .summary = "xff's license in full (Apache-2.0)"},
      {.name = "full", .aliases = {"long"}, .summary = "every option and primary, with the long explanations"},
  };
}

std::string RenderTopicIndex(std::string_view indent, std::size_t name_width) {
  std::string out;
  for (const HelpTopic& topic : HelpTopics()) {
    std::string summary(topic.summary);
    if (!topic.aliases.empty()) {
      absl::StrAppend(&summary, " (also: ", absl::StrJoin(topic.aliases, ", "), ")");
    }
    const std::size_t pad = name_width > topic.name.size() ? name_width - topic.name.size() : 2;
    absl::StrAppend(&out, indent, topic.name, std::string(pad, ' '), summary, "\n");
  }
  return out;
}

std::string RenderDocRows(
    std::string_view indent,
    const std::vector<std::pair<std::string_view, std::string_view>>& rows) {
  std::size_t width = 0;
  for (const auto& [code, detail] : rows) {
    width = std::max(width, code.size());
  }
  width += 2;  // a 2-space gap after the widest code
  std::string out;
  for (const auto& [code, detail] : rows) {
    absl::StrAppend(&out, indent, code, std::string(width - code.size(), ' '), detail, "\n");
  }
  return out;
}

std::string RenderOptions(std::string_view group_indent) {
  constexpr std::size_t kWidth = 30;  // summary column; a display longer than this gets a 2-space gap
  const auto row = [&](std::string_view display, std::string_view summary) {
    const std::size_t pad = display.size() + 2 <= kWidth ? kWidth - display.size() : 2;
    return absl::StrCat(group_indent, "  ", display, std::string(pad, ' '), summary, "\n");
  };
  std::string out;
  std::string extras;  // flags whose build extra is not compiled into this binary (deferred)
  std::string_view group;
  for (const GlobalFlag& flag : Globals()) {
    // An extra flag that is NOT built in stays listed, but under a distinct "Extras" group with a
    // note on what to rebuild with (the flag/extra are the SOT; the hint is derived). When the extra
    // IS built in, the flag shows in its normal group like any other.
    if (!flag.extra.empty() && !ExtraEnabled(flag.extra)) {
      absl::StrAppend(&extras, row(flag.display, absl::StrCat(flag.summary, "  [needs --//xff:", flag.extra, "]")));
      continue;
    }
    if (flag.group != group) {
      group = flag.group;
      absl::StrAppend(&out, "\n", group_indent, flag.header, ":\n");  // group is the key; header is the display
    }
    absl::StrAppend(&out, row(flag.display, flag.summary));
  }
  if (!extras.empty()) {
    absl::StrAppend(&out, "\n", group_indent, "Extras (not built into this binary):\n", extras);
  }
  return out;
}

std::vector<HelpFlag> HelpFlags() {
  return {
      {.display = "-h, --help, -help", .summary = "print this usage page and exit (-help for GNU find compatibility)"},
      {.display = "--help=NAME", .summary = "full help for one option or primary (e.g. --help=-regex, --help=--sort)"},
      {.display = "--help=TOPIC", .summary = "detailed help for a topic:"},
      {.display = "--help-full", .summary = "the full detailed reference (also --help-long); --help-all = --help=all"},
      {.display = "--man", .summary = "print the man page (roff; pipe to `mandoc` or GNU `man -l -`) and exit"},
      {.display = "--markdown", .summary = "print a Markdown reference of all options and primaries and exit"},
      {.display = "--version, -version", .summary = "print the version and exit"},
  };
}

std::string RenderHelpSection() {
  constexpr std::size_t kWidth = 21;  // widest display ("--version, -version") + a 2-space gap
  std::string out = "\n  Help:\n";
  for (const HelpFlag& flag : HelpFlags()) {
    const std::size_t pad = flag.display.size() + 2 <= kWidth ? kWidth - flag.display.size() : 2;
    absl::StrAppend(&out, "    ", flag.display, std::string(pad, ' '), flag.summary, "\n");
    if (flag.display == "--help=TOPIC") {
      // Nest the topic list under --help=TOPIC (8-space indent); name_width 19 lands the
      // topic summaries at column 27 -- a deliberate +2 past the flag summaries (col 25)
      // so the nested list reads as sub-items rather than a broken grid.
      absl::StrAppend(&out, RenderTopicIndex("        ", 19));
    }
  }
  return out;
}

absl::StatusOr<std::string> RenderHelp(std::string_view topic) {
  if (topic.empty() || topic == "list") {
    return RenderIndex();
  }
  if (topic == "help") {
    return RenderHelpGuide();  // the help-system guide + topic index
  }
  if (topic == "all") {
    return RenderFull(/*detailed=*/false);  // every option + primary, summaries only
  }
  if (topic == "full" || topic == "long") {
    return RenderFull(/*detailed=*/true);  // every option + primary, with the long explanations
  }
  if (topic == "expressions") {
    return RenderExpressions();  // the annotated Tests/Actions/Operators list, sans globals
  }
  if (topic == "fields") {
    return RenderFields();  // the {field} placeholder vocabulary (--help=format is the --format flag)
  }
  if (topic == "stats") {
    return RenderStats();  // the --summary / --histogram reductions
  }
  if (topic == "config") {
    return RenderConfig();  // config tiers, style selection, and arming
  }
  if (topic == "cookbook" || topic == "examples" || topic == "recipes") {
    return RenderCookbook();  // worked examples composing the building blocks end to end
  }
  if (topic == "notice" || topic == "notices") {
    return RenderNotice();  // third-party component manifest + what this binary contains
  }
  if (topic == "license" || topic == "licenses") {
    return RenderLicense();  // xff's own license (Apache-2.0), in full
  }
  // Expression primary / operator / action (leading-dash convenience: `--help=regex`).
  const registry::Descriptor* descriptor = registry::Lookup(topic);
  if (descriptor == nullptr && topic.front() != '-' && topic.front() != '!') {
    descriptor = registry::Lookup(absl::StrCat("-", topic));
  }
  if (descriptor != nullptr) {
    return RenderOne(*descriptor, /*with_details=*/true);
  }
  // Whole-run global option (leading-dashes convenience: `--help=sort`).
  const GlobalFlag* global = LookupGlobal(topic);
  if (global == nullptr && topic.front() != '-') {
    global = LookupGlobal(absl::StrCat("--", topic));
  }
  if (global != nullptr) {
    return RenderGlobalFlag(*global, /*with_details=*/true);  // single-entry help shows the long explanation
  }
  return absl::NotFoundError("");  // the caller holds the topic and composes the message
}

}  // namespace xff::cli
