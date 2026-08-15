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

#include "xff/cli/help_build.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/types/span.h"
#include "xff/archive/archive_backend.h"
#include "xff/cli/globals.h"
#include "xff/cli/help.h"
#include "xff/cli/help_model.h"
#include "xff/cli/help_parse.h"
#include "xff/datetime/datetime.h"
#include "xff/engine/evaluate.h"
#include "xff/fields/fields.h"
#include "xff/license/license.h"
#include "xff/regex/regex.h"
#include "xff/registry/descriptor.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

using DocPair = std::pair<std::string_view, std::string_view>;

// The expression registry split into the three EXPRESSION subsections, in display order.
struct KindSection {
  registry::Kind kind;
  std::string_view title;
};

constexpr std::array<KindSection, 3> kKindSections = {{
    {.kind = registry::Kind::kTest, .title = "Tests"},
    {.kind = registry::Kind::kAction, .title = "Actions"},
    {.kind = registry::Kind::kOperator, .title = "Operators"},
}};

// A single prose paragraph block from an authored string (backtick inline markup).
Content ProseOf(std::string_view text) {
  return Content{.node = Prose{.runs = ParseInline(text)}};
}

// A verbatim example block (no wrapping); optional info string for the fenced backends.
Content ExampleOf(std::string text, std::string lang = "") {
  return Content{.node = Example{.text = std::move(text), .lang = std::move(lang)}};
}

// A term/description vocabulary table; the description carries backtick markup.
Content RowsOf(absl::Span<const DocPair> pairs) {
  Rows rows;
  rows.rows.reserve(pairs.size());
  for (const auto& [term, desc] : pairs) {
    rows.rows.push_back(Row{.term = std::string(term), .description = ParseInline(desc)});
  }
  return Content{.node = std::move(rows)};
}

// The classification tags after a flag's term, e.g. (global, xff).
std::vector<std::string> FlagTags(const GlobalFlag& flag) {
  return {"global", flag.xff ? "xff" : "find"};
}

// The classification tags after a primary's term, e.g. (test, find) or
// (action, xff, runs commands).
std::vector<std::string> PrimaryTags(const registry::Descriptor& descriptor) {
  std::vector<std::string> tags;
  switch (descriptor.kind) {
    case registry::Kind::kAction: tags.emplace_back("action"); break;
    case registry::Kind::kOperator: tags.emplace_back("operator"); break;
    case registry::Kind::kTest: tags.emplace_back("test"); break;
  }
  tags.emplace_back(descriptor.style == registry::Style::kXff ? "xff" : "find");
  if (descriptor.safety == registry::Safety::kSecurity) {
    tags.emplace_back("runs commands");
  } else if (descriptor.safety == registry::Safety::kSafety) {
    tags.emplace_back("modifies the filesystem");
  }
  return tags;
}

// Every global flag whose `affects` list names `name` (a flag or primary), in
// Globals() display order - the reverse of the forward `affects` declaration, so the
// "Affected by:" block on any entry stays in lock-step with the flags' declarations.
std::vector<std::string_view> AffectedByFlags(std::string_view name) {
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

// Appends an influence detail line ("Header: a, b, c") to `blocks` when non-empty.
void AppendInfluence(Blocks* blocks, std::string_view header, const std::vector<std::string_view>& names) {
  if (names.empty()) {
    return;
  }
  blocks->push_back(ProseOf(absl::StrCat(header, " ", absl::StrJoin(names, ", "))));
}

// The heading line above a flag's value table: "GROUP is one of:" for a display that collapsed its
// value grammar to a `<PLACEHOLDER>` (e.g. "--summary[=<GROUP>]"), so the rows read as that
// placeholder's values. A display that spells its values inline instead (`--archive[=none|roots|all]`)
// has no placeholder to name, and gets the bare "One of:" - naming a placeholder that is not there
// used to read as the nonsense "One of is one of:".
std::string ValueHeading(std::string_view display) {
  const auto open = display.find('<');
  const auto close = display.find('>', open + 1);
  if (open == std::string_view::npos || close == std::string_view::npos) {
    return "One of:";
  }
  return absl::StrCat(display.substr(open + 1, close - open - 1), " is one of:");
}

// A definition entry for a global flag: its display, summary, and (when `with_details`)
// its detail blocks enriched with a "not built into this binary" note when its build
// extra is absent and the Affects / Affected-by influence blocks. `with_details` is
// false for the terse usage page (summary + tags only).
Content FlagEntry(const GlobalFlag& flag, bool with_details = true, Audience audience = Audience::kThisBinary) {
  Blocks details;
  // The not-built note shows even on the terse usage page: a flag whose build extra is
  // absent is a hard error if used, so the reader must see it up front. It is OMITTED for a
  // published reference, which documents the tool rather than whichever binary generated it -
  // the flag's own details still say it is a build extra and name the flag to rebuild with.
  if (audience == Audience::kThisBinary && !flag.extra.empty() && !ExtraEnabled(flag.extra)) {
    details.push_back(ProseOf(
        absl::StrCat(
            "NOT built into this binary: rebuild with `", ExtraBuildFlag(flag.extra),
            "` (used as-is, it is a hard error).")));
  }
  if (with_details) {
    // The allowed-value table (for a flag whose synopsis collapsed a value grammar to a
    // `<PLACEHOLDER>`) leads, before the prose: a "<LABEL> is one of:" heading line (see
    // ValueHeading) so the
    // rows read as that placeholder's values, then the aligned wrapping `value  meaning` list.
    if (!flag.values.empty()) {
      details.push_back(ProseOf(ValueHeading(flag.display)));
      Rows rows;
      rows.rows.reserve(flag.values.size());
      for (const ValueDoc& value : flag.values) {
        if (value.hidden) {
          continue;  // an alias or reserved name: accepted by the check, not listed here
        }
        rows.rows.push_back(Row{.term = std::string(value.value), .description = ParseInline(value.meaning)});
      }
      // A title-less subsection nests the value table one level deeper than its label.
      Subsection group;
      group.children.push_back(Content{.node = std::move(rows)});
      details.push_back(Content{.node = std::move(group)});
    }
    for (Content& block : ParseBlocks(flag.details)) {
      details.push_back(std::move(block));
    }
    AppendInfluence(&details, "Affects:", absl::StrSplit(flag.affects, ',', absl::SkipEmpty()));
    AppendInfluence(&details, "Affected by:", AffectedByFlags(flag.name));
  }
  return Content{
      .node = Entry{
          .term = std::string(flag.display),
          .summary = ParseInline(flag.summary),
          .details = std::move(details),
          .xff = flag.xff,
          .tags = FlagTags(flag),
      }};
}

// A definition entry for an expression primary: its synopsis, summary, and (when
// `with_details`) its detail blocks + the Affected-by block. `with_details` is false
// for the terse usage page (summary + tags only).
Content PrimaryEntry(const registry::Descriptor& descriptor, bool with_details = true) {
  Blocks details;
  if (with_details) {
    details = ParseBlocks(descriptor.details);
    AppendInfluence(&details, "Affected by:", AffectedByFlags(descriptor.name));
  }
  return Content{
      .node = Entry{
          .term = absl::StrCat(descriptor.name, ArgHint(descriptor)),
          .summary = ParseInline(descriptor.summary),
          .details = std::move(details),
          .xff = descriptor.style == registry::Style::kXff,
          .tags = PrimaryTags(descriptor),
      }};
}

// FIELDS: the named {field} vocabulary as grouped subsections of rows, then the brace
// rules, dynamic namespaces, and qualifiers. Mirrors WriteFields() as model nodes.
Section BuildFields() {
  Section section{.title = "Fields"};
  section.children.push_back(ProseOf(
      "The `{field}` placeholder vocabulary, substituted per entry in --template / --format, in "
      "-printf via the `%{field}` escape, and (with --exec-fields) in -exec."));

  const std::vector<fields::FieldDoc> docs = fields::FieldDocs();
  std::string_view group;
  Subsection current;
  bool have_current = false;
  Rows rows;
  const auto flush = [&] {
    if (have_current) {
      if (!rows.rows.empty()) {
        current.children.push_back(Content{.node = std::move(rows)});
        rows = Rows{};
      }
      section.children.push_back(Content{.node = std::move(current)});
    }
  };
  for (const fields::FieldDoc& doc : docs) {
    if (doc.group != group) {
      flush();
      group = doc.group;
      current = Subsection{.title = std::string(doc.header)};
      have_current = true;
    }
    std::string term = absl::StrCat("{", doc.name, "}");
    for (const std::string_view alias : doc.aliases) {
      absl::StrAppend(&term, " {", alias, "}");
    }
    rows.rows.push_back(Row{.term = std::move(term), .description = ParseInline(doc.summary)});
  }
  flush();

  Subsection braces{.title = "Braces"};
  braces.children.push_back(
      Content{
          .node = Bullets{
              .items = {
                  ParseInline("`{{` and `}}` emit literal braces"),
                  ParseInline("`{}` is an alias for `{path}`"),
                  ParseInline("an unknown field renders empty"),
                  ParseInline("a malformed or unterminated `{` stays literal"),
              }}});
  section.children.push_back(Content{.node = std::move(braces)});

  static constexpr std::array<DocPair, 4> kDynamicNamespaces = {{
      {"{0}..{N}", "-regex captures ({0} the whole match, {1}..{N} the groups)"},
      {"{env.NAME}", "a process environment variable"},
      {"{def.NAME}", "a --define value"},
      {"{capture.NAME}", "a -capture command result"},
  }};
  Subsection dynamic{.title = "Dynamic namespaces"};
  dynamic.children.push_back(RowsOf(kDynamicNamespaces));
  section.children.push_back(Content{.node = std::move(dynamic)});

  const std::string path_comp = absl::StrCat(
      "path component of the value: ", absl::StrJoin(fields::PathComponentKeywords(), "|"),
      "; any path-valued field composes, e.g. {relpath:stem}, {def.B:dir}");
  const std::array<DocPair, 6> qualifiers = {{
      {"{mtime:FMT}", "time format: strftime (%Y-%m-%d) or preset (iso, epoch); see --time-format / --timezone"},
      {"{size:h}", "human-readable size"},
      {"{name:s/RE/R/f}", "RE2 rewrite of the value (flags g=all, i=ignore-case; any delimiter)"},
      {"{cap:m/RE/R/f}",
       "per-line extraction: a value stream, e.g. a --summary key (m//, s///'s list-producing sibling)"},
      {"{cap:m/RE/R/;join(SEP)}",
       "reduce the stream to one scalar (join, SEP default newline) so m// is usable in a scalar context "
       "(-printf / --template / -exec); reducers are function-notation, e.g. join(, )"},
      {"{path:COMP}", path_comp},
  }};
  Subsection quals{.title = "Qualifiers ({field:QUAL})"};
  quals.children.push_back(RowsOf(qualifiers));
  quals.children.push_back(ProseOf(
      "An m// extraction is a left-to-right pipeline: s/// maps whatever is flowing (each line, then "
      "the scalar), and a terminal reducer such as join collapses the stream to one scalar."));
  quals.children.push_back(ExampleOf(
      "  {cap:m/PAT/REP/;s/PAT/REP/;join(SEP);s/PAT/REP/}\n"
      "       |________| |________| |_______| |________|\n"
      "       extract    map each   reduce    rewrite\n"
      "       per line   line       stream    scalar"));
  quals.children.push_back(ProseOf(
      "For -printf's own % directives (%p %f %s %t ...) and the `%{field}` escape that bridges them "
      "to this vocabulary, see the Printf directives (`--help=-printf`)."));
  section.children.push_back(Content{.node = std::move(quals)});
  return section;
}

// Builds one section whose children are a lead prose paragraph followed by a vocabulary table.
Section VocabSection(std::string_view title, std::string_view prose, absl::Span<const DocPair> pairs) {
  Section section{.title = std::string(title)};
  section.children.push_back(ProseOf(prose));
  section.children.push_back(RowsOf(pairs));
  return section;
}

// The sub-vocabulary sections, each also standalone as its own `--help=TOPIC` (see
// TopicReference). Named so BuildReference and the topic render share one definition.
Section PrintfSection() {
  return VocabSection(
      "Printf directives", "Directives for -printf / -fprintf / -println FORMAT, and the `%{field}` escape.",
      engine::PrintfDocs());
}

Section TimeSection() {
  Section section = VocabSection(
      "Time formats", "Presets and strftime patterns for --time-format, --timezone, and time-field {:qualifiers}.",
      datetime::FormatDocs());
  section.children.push_back(ProseOf(
      "Time zone. `--timezone=ZONE` (alias `--tz`) sets the zone every time is interpreted and rendered in: "
      "`local` (the default), `utc` (also `z` / `zulu`), an IANA name like `Europe/London`, or a fixed offset "
      "like `+02:00` / `-0800`. It shifts the wall-clock digits of every time field and governs `-newerXt` "
      "comparisons; it does not by itself add or remove the printed zone suffix."));
  section.children.push_back(ProseOf(
      "Zone suffix. `--time-zone-suffix=never` drops the trailing offset (`+0100`, `+01:00`) from a preset that "
      "shows it by default (`space`, `iso` / `iso8601-*`, `rfc3339`); `always` forces one on, even onto `asctime` "
      "which omits it; `auto` (the default) keeps each preset's built-in behavior. `true` / `false` are accepted "
      "for `always` / `never`. Two things it never touches: the inherently-zoned `zulu` / `zulu-dense` / `asn1z` "
      "keep their mandatory `Z` (UTC is the format's identity), and a custom strftime `--time-format` is left "
      "exactly as written - control its zone there with `%z` / `%Ez` / `%Z` yourself. `asn1`'s zone is optional, "
      "so `always` appends its ASN.1-style offset (`+0100`, no separator) and `never` / `auto` leave it bare."));
  return section;
}

Section SizeSection() {
  return VocabSection("Size units", "Units for -size / -blocks [+|-]N[unit].", engine::SizeUnitDocs());
}

Section GrammarsSection() {
  return VocabSection(
      "Regex grammars",
      "The grammar for -regex / -iregex and the content matchers -rxc / -grep, chosen by `--regextype` "
      "(default RE2). EXACT, FNMATCH, GLOB and SHGLOB are core engines, always built in; PCRE2 is a "
      "build-time extra (see `--help=extras`). RE2 and PCRE2 have canonical external references, so the "
      "smaller engines are spelled out in full here: they have no single authoritative man page, and "
      "FNMATCH delegates to the platform's fnmatch(3), whose class / collation details vary by system.",
      regex::GrammarDocs());
}

// ENVIRONMENT: the variables xff honors. Hand-authored - the getenv sites are scattered across
// color / pager / help-width / config with no single registry - and kept honest by help_render_test.
// Standalone as `--help=environment` (env) and folded into the full reference / man page.
Section EnvironmentSection() {
  Section env{.title = "Environment"};
  env.children.push_back(ProseOf(
      "Environment variables xff reads. An explicit command-line flag generally overrides the matching "
      "variable."));
  static constexpr auto kVars = std::to_array<DocPair>({
      {"NO_COLOR",
       "when set (any value), disables color like `--color=never`; `--color=always` still wins "
       "(https://no-color.org)"},
      {"XFF_PAGER",
       "the pager for the long `--help` / `--markdown` output, and for the file listing under "
       "`--pager=all` (see `--pager`); overrides `$PAGER`; set empty to disable paging"},
      {"PAGER", "the pager used when `$XFF_PAGER` is unset"},
      {"XFF_MANPAGER",
       "the pager / formatter for `--man`; overrides the built-in `mandoc` pipeline; set empty to disable"},
      {"COLUMNS", "terminal width used to wrap plain `--help` text for `--width=auto` when the tty size is unknown"},
      {"XFF_CONFIG",
       "explicit path to the config file, taking precedence over the XDG / HOME search (see `--help=config`)"},
      {"XDG_CONFIG_HOME", "config search root: `$XDG_CONFIG_HOME/xff/config` (see `--help=config`)"},
      {"HOME", "config fallback: `$HOME/.config/xff/config` when `$XDG_CONFIG_HOME` is unset"},
      {"LC_ALL, LC_CTYPE, LANG",
       "locale for `--unicode=auto`: a UTF-8 locale selects the Unicode `--format=tree` connectors, else ASCII"},
      {"LSCOLORS",
       "the same theme in BSD / macOS spelling (11 letter pairs); read when `$LS_COLORS` is unset, which "
       "is what makes a themed macOS shell work (see `--color-scheme`)"},
      {"LS_COLORS",
       "the terminal's colour theme, as `ls` / `dircolors` set it: type keys (`di`, `ln`, `ex`, ...) and "
       "per-extension `*.tar=` entries, used by default (see `--color-scheme`)"},
      {"XDG_RUNTIME_DIR",
       "preferred directory for a member extracted by `--archive-extract`: it is a memory-backed tmpfs, so "
       "the copy never reaches a disk (`/dev/shm` is tried next)"},
      {"TMPDIR",
       "where a temporary file goes when no memory-backed directory fits it: an extracted member "
       "(`--archive-extract`) and the in-progress rewrite of a container (`--archive-delete`)"},
  });
  env.children.push_back(RowsOf(kVars));
  env.children.push_back(ProseOf(
      "Any process environment variable is also readable in the field vocabulary as `{env.NAME}` (see "
      "`--help=fields`)."));
  return env;
}

// STATISTICS: the two terminal reductions (--summary / --histogram). The flags are
// pulled from the globals SOT via the "stats" topic tag so the list cannot drift, then
// worked examples. Standalone as `--help=stats` (see TopicReference) and folded into
// the full reference. `in_full` (the folded-in case) omits the per-flag entries: the
// full reference already documents every flag in its grouped Options section, so
// repeating them here is pure duplication - the narrative + examples are what add value.
Section StatsSection(bool in_full) {
  Section section{.title = "Statistics"};
  section.children.push_back(ProseOf(
      "xff statistics reductions. `--summary` and `--histogram` replace the per-match listing with an "
      "aggregate over all matches; they are independent and combinable (one walk feeds both), and an "
      "explicit action (`-print` / `-exec`) still runs. `--format=jsonl` emits machine rows instead."));
  for (const GlobalFlag& flag : Globals()) {
    if (!in_full && flag.topic == "stats") {
      section.children.push_back(FlagEntry(flag));
    }
  }
  static constexpr std::array<DocPair, 5> kExamples = {{
      {"xff --summary=ext", "files + total size per extension"},
      {"xff --histogram=ext", "a bar chart of files per extension"},
      {"xff --histogram='ext:sum(lines)'", "total lines per extension"},
      {"xff --histogram=size", "the file-size distribution"},
      {"xff --summary=type --histogram=ext --format=jsonl", "both, as machine rows"},
  }};
  Subsection examples{.title = "Examples"};
  // Each example is a verbatim (copy-pastable) command with its explanation as prose,
  // which wraps to the width - the cookbook pattern, not a term/desc table whose wide
  // command column squeezes the explanation to one word per line at narrow widths.
  for (const auto& [command, explanation] : kExamples) {
    examples.children.push_back(ExampleOf(std::string(command), "sh"));
    examples.children.push_back(ProseOf(explanation));
  }
  section.children.push_back(Content{.node = std::move(examples)});
  return section;
}

// ARCHIVES: what it means to walk INTO a container, and the whole `--archive` family. The flags are
// pulled from the globals SOT via the "archive" topic tag so the list cannot drift; the identity /
// read-only / cost rules are prose, because they are what a reader needs before the flags mean
// anything. Standalone as `--help=archive` (see TopicReference) and folded into the full reference.
// `in_full` (the folded-in case) omits the per-flag entries, which the grouped Options section
// already carries.
Section ArchiveSection(bool in_full) {
  Section section{.title = "Archives"};
  section.children.push_back(ProseOf(
      "With `--archive`, an archive is a directory: xff opens it and walks its members as ordinary "
      "entries, so every predicate and action applies to them unchanged - `-name`, `-type`, "
      "`-grep`, `{hash}`, `--summary`. Nothing in the expression vocabulary knows about archives. "
      "Needs the archive extra; `--help=extras` says whether this binary has it."));

  static constexpr std::array<DocPair, 4> kModes = {{
      {"none", "an archive is one plain file (find's behaviour, and the find-style default)"},
      {"roots", "dive only when a search root IS an archive (the xff-family default)"},
      {"all", "dive archives met during the walk too (what a bare `--archive` selects)"},
      {"any", "`all`, and offer EVERY file to the reader rather than only container-looking names"},
  }};
  Subsection modes{.title = "How far diving goes"};
  modes.children.push_back(RowsOf(kModes));
  modes.children.push_back(ProseOf(
      "Two axes, spelled independently: the RUNG says how much to look at, the CASE of the short "
      "form says whether writing is armed. So a slipped shift key changes the capability, never the "
      "level - and arming is not doing: something still has to ask for a write, and `--safe` / "
      "`--dry-run` still apply."));
  modes.children.push_back(ProseOf(
      "Later wins, per axis, which is what makes the two useful together: `-Z++ -z-` arms writing "
      "with reading OFF - write archives without diving into existing ones to harvest members - "
      "while `-Z-` is the full reset, turning reading off AND disarming writing whatever an earlier "
      "flag or a config file asked for. A lower-case form never disarms; only `-Z-` does."));
  modes.children.push_back(
      Content{
          .node = Example{
              .text = "                   read only    + write (--archive-write)\n"
                      "  none              -z-          -Z-  (also disarms writing)\n"
                      "  roots (default)   -z           -Z\n"
                      "  all               -z+          -Z+\n"
                      "  any               -z++         -Z++",
              .lang = "text"}});
  modes.children.push_back(ProseOf(
      "Under `all` a file is only opened when its NAME looks like a container, so walking a source "
      "tree does not read every file in it; `any` (also spelled `--archive-any`) drops that gate. "
      "Nesting has its own cap (`--archive-depth`, default 1) because a container inside a container "
      "is where a decompression bomb lives - and it is deliberately NOT part of any rung, since "
      "raising the bomb cap is a different decision from looking in more places. `-maxdepth` keeps "
      "counting member levels as ordinary depth."));
  section.children.push_back(Content{.node = std::move(modes)});

  Subsection identity{.title = "A member is an entry, a container is still a file"};
  identity.children.push_back(ProseOf(
      "A member's path is the container's, the separator, then the member: `a.tar!dir/two.txt` "
      "(`--archive-separator` / `--archive-prefix` spell it differently). The container keeps its own "
      "identity at the same time - it is a real `-type f` you can match and delete - so a dive shows "
      "you both, which is also why `--archive-aggregate` exists: a reduction that counted the "
      "container AND its members would describe no filesystem that exists."));
  identity.children.push_back(ProseOf(
      "Members are READ-ONLY by default. `-delete` and the exec family refuse one rather than "
      "silently doing nothing, because a member has no path a process can open and no way to be "
      "unlinked; `--archive-extract` runs the child over a temporary copy, and `--archive-delete` "
      "rewrites the container without the member. Both are opt-in, and both say so in the refusal "
      "you get without them."));
  section.children.push_back(Content{.node = std::move(identity)});

  Subsection creating{.title = "Creating one"};
  creating.children.push_back(ProseOf(
      "`--pack=FILE` turns the walk around: every match is written into a NEW archive instead of "
      "being listed, so the member list is an expression rather than a pipeline into `tar`. The "
      "output name picks the format, each member keeps the path it had relative to its search root, "
      "and `--sort` decides the order inside. It is a sink like `--summary`, the archive appears only "
      "when the walk finished, and a member of another container is refused - harvesting files out of "
      "one archive to re-pack them into another is a separate feature, which is also what `-Z++ -z-` "
      "is reserved for."));
  // Same rule as the option table below: the formats come from the LINKED writer, so a format added
  // to the extra cannot leave this list behind.
  const std::vector<std::string> pack_formats = archive::ContainerPackFormats();
  if (!pack_formats.empty()) {
    // Rendered WITH the leading dot: these are the filename suffixes a user types after `--pack=`,
    // and the dotless form is the one spelling nobody writes.
    creating.children.push_back(ProseOf(
        absl::StrCat("Output filename suffixes this binary writes: `.", absl::StrJoin(pack_formats, "`, `."), "`.")));
  }
  // The vocabulary comes from the LINKED writer, never from a list kept here: a lean binary then
  // simply has no table (it has no packer either), and adding an option to the extra cannot leave the
  // documentation behind.
  const std::vector<archive::PackOptionInfo> pack_options = archive::ContainerPackVocabulary();
  if (!pack_options.empty()) {
    creating.children.push_back(ProseOf(
        "`--pack-option=NAME=VALUE` (repeatable, last value for a NAME wins) tunes the writer. The "
        "names are xff's own and are translated for whichever library does the writing, so an unknown "
        "one is a usage error and this list is exactly what THIS binary accepts:"));
    Rows rows;
    rows.rows.reserve(pack_options.size());
    for (const archive::PackOptionInfo& option : pack_options) {
      rows.rows.push_back(
          Row{.term = absl::StrCat(option.name, "=", option.value_syntax),
              .description = ParseInline(absl::StrCat(option.detail, " (", option.formats, ")"))});
    }
    creating.children.push_back(Content{.node = std::move(rows)});
  }
  creating.children.push_back(ProseOf(
      "PHP phars are the exception: xff reads them and can rewrite one to remove members, but it does "
      "not CREATE one, because a phar is a PHP program with a stub, a manifest and a signature rather "
      "than a container of files. Build one with `box` (box-project/box) or PHP's own `Phar` class, "
      "and verify or install one with `phive` (phar-io/phive), which checks the signature xff will "
      "not forge."));
  section.children.push_back(Content{.node = std::move(creating)});

  for (const GlobalFlag& flag : Globals()) {
    if (!in_full && flag.topic == "archive") {
      section.children.push_back(FlagEntry(flag));
    }
  }

  static constexpr std::array<DocPair, 6> kExamples = {{
      {"xff --archive=roots a.tar", "list the archive and its members"},
      {"xff -z+ . -grep TODO", "search inside every archive met in the tree"},
      {"xff --archive=roots a.tgz --summary", "count what is INSIDE, not the compressed container"},
      {"xff --archive=roots --archive-extract a.tar -name '*.json' -exec jq . {} \\;",
       "run a tool over a member, via a temporary copy"},
      {"xff --archive=roots --archive-delete a.tar -name '*.bak' -delete", "rewrite the archive without those members"},
      {"xff . -name '*.cc' -newer VERSION --pack=changed.tar.gz",
       "pack what the expression matched into a new archive"},
  }};
  Subsection examples{.title = "Examples"};
  for (const auto& [command, explanation] : kExamples) {
    examples.children.push_back(ExampleOf(std::string(command), "sh"));
    examples.children.push_back(ProseOf(explanation));
  }
  section.children.push_back(Content{.node = std::move(examples)});
  return section;
}

// CONTENT: what it means to read INSIDE files, and the whole content-matching family. The primaries
// are pulled from the registry SOT via Descriptor.topic and the flags via GlobalFlag.topic, so the
// lists cannot drift; the cross-cutting rules are prose, because they are what a reader needs before
// the family means anything. Standalone as `--help=content` and folded into the full reference.
Section ContentSection(bool in_full) {
  Section section{.title = "Content"};
  section.children.push_back(ProseOf(
      "These primaries read the entry's BYTES, not its metadata: `-grep` prints matching lines the "
      "way ripgrep does, `-content` / `-icontent` test for a literal, `-rxc` / `-irxc` for a regex "
      "(grammar per `--regextype`, see `--help=grammars`), and `-text` / `-eofcr` / `-eofcrlf` "
      "classify line endings and completeness. `{lines}`, `{text}`, `{line}`, `{match}` and "
      "`{column}` carry the results into templates (`--help=fields`)."));
  section.children.push_back(ProseOf(
      "Every one of them reads through the entry's OWN filesystem, so under `--archive` a member is "
      "searched inside its container exactly like a plain file - `a.tar!notes.txt` greps without "
      "unpacking anything. Reading is per entry and streamed, so a match in a huge tree costs the "
      "bytes of the files visited, not of the tree."));
  for (const registry::Descriptor& descriptor : registry::All()) {
    if (!in_full && descriptor.topic == "content") {
      section.children.push_back(PrimaryEntry(descriptor));
    }
  }
  for (const GlobalFlag& flag : Globals()) {
    if (!in_full && flag.topic == "content") {
      section.children.push_back(FlagEntry(flag));
    }
  }
  static constexpr std::array<DocPair, 3> kExamples = {{
      {"xff src -name '*.cc' -grep 'TODO\\('", "matching lines, rg-style, from the files an expression picked"},
      {"xff . -type f ! -text", "the files that are NOT line-oriented text"},
      {"xff -z logs.tar -grep ERROR --count", "per-member match counts inside an archive"},
  }};
  Subsection examples{.title = "Examples"};
  for (const auto& [command, explanation] : kExamples) {
    examples.children.push_back(ExampleOf(std::string(command), "sh"));
    examples.children.push_back(ProseOf(explanation));
  }
  section.children.push_back(Content{.node = std::move(examples)});
  return section;
}

// CONFIGURATION: how options resolve (layered tiers + the command line), how a style is
// chosen (--config / argv[0]), and how dangerous --xffrc directives are armed. The flags
// are pulled from the globals SOT via the "config" topic tag so the list cannot drift;
// the layering / argv[0] / arming rules are prose. Standalone as `--help=config` (see
// TopicReference) and folded into the full reference. `in_full` (the folded-in case) drops
// the per-flag "Config flags" subsection: the full reference's grouped Options section
// already documents each flag, so the layering / style / arming prose is all that adds value.
Section ConfigSection(bool in_full) {
  Section section{.title = "Configuration"};
  section.children.push_back(ProseOf(
      "xff configuration. Options resolve from layered config tiers, then the command line; later "
      "layers win. A style (find / xff / rg) sets the baseline defaults, which the tiers and the "
      "command line then adjust. Run `--explain` to print exactly what resolved."));

  static constexpr std::array<DocPair, 4> kLayers = {{
      {"system config", "machine-wide defaults (+ a root-owned [policy] that can hard-deny arming)"},
      {"user config", "your personal defaults"},
      {"--xffrc=FILE", "an explicitly named file (repeatable) - a NON-ARMING tier"},
      {"command line", "flags and --config, highest"},
  }};
  Subsection layers{.title = "Layers (lowest to highest precedence)"};
  layers.children.push_back(RowsOf(kLayers));
  layers.children.push_back(ProseOf(
      "There is no project / ancestor .xffrc discovery: config comes from the system and user files "
      "plus any `--xffrc` you name. `--no-config` ignores the discovered system/user files."));
  section.children.push_back(Content{.node = std::move(layers)});

  Subsection style{.title = "Choosing a style"};
  style.children.push_back(ProseOf(
      "`--config=NAME` selects find / xff / rg (repeatable, last wins); see `--help=styles` for the "
      "table. The invocation name (argv[0]) is the leading selector, so a symlink named `find` runs the "
      "strict find style and `rg` the rg style; any other name (e.g. a `mytool` symlink) activates a "
      "same-named config block over the xff default. An explicit `--config` still stacks on top."));
  section.children.push_back(Content{.node = std::move(style)});

  Subsection arming{.title = "Arming dangerous directives"};
  arming.children.push_back(ProseOf(
      "A dangerous directive (the exec family -exec/-execdir/-ok/-capture, or -delete) carried by an "
      "--xffrc file is inert unless `--allow-exec` is set from a TRUSTED tier (the command line or the "
      "system/user config, never an --xffrc file itself). Unarmed lines are dropped with a warning; the "
      "root system [policy] can hard-deny even `--allow-exec`."));
  section.children.push_back(Content{.node = std::move(arming)});

  if (!in_full) {
    Subsection flags{.title = "Config flags"};
    for (const GlobalFlag& flag : Globals()) {
      if (flag.topic == "config") {
        flags.children.push_back(FlagEntry(flag));
      }
    }
    section.children.push_back(Content{.node = std::move(flags)});
  }
  return section;
}

// The `--help=notice` topic (alias notices): the one build-dependent line (which extras THIS
// binary contains, via ExtraEnabled) then the third-party component manifest, reproduced
// verbatim from the compiled-in repo NOTICE so a single-file release is self-contained. A
// title-less section renders the manifest verbatim at column 0 (an Example is never wrapped).
Section NoticeSection() {
  Section section;  // title-less: no heading, body at column 0
  section.children.push_back(ProseOf(
      absl::StrCat(
          "Build extras compiled into this binary: ", ExtraEnabled("archive") ? "archive" : "none (lean build)")));
  section.children.push_back(Content{.node = Example{.text = license::NoticeText()}});
  return section;
}

// The `--help=license` topic (alias licenses): xff's own license (Apache-2.0) in full, led by
// the copyright + grant statement (task #142). Legal text renders verbatim and must not reflow,
// so it is a title-less section holding a single Example (column 0, byte-exact, unwrapped).
Section LicenseSection() {
  Section section;  // title-less: the copyright leads, then the license body, verbatim
  section.children.push_back(
      Content{.node = Example{.text = absl::StrCat(license::CopyrightNotice(), license::LicenseText())}});
  return section;
}

// EXAMPLES: the cookbook recipes as structured nodes - each a subsection with the
// verbatim command (an Example, kept copy-pastable) and its explanation (Prose, which
// wraps). The recipe list is the SOT in help.cc, run end to end by cookbook_test.
Section BuildExamples() {
  Section section{.title = "Examples"};
  section.children.push_back(ProseOf(
      "Worked examples that compose xff's building blocks. Each shows a task, its command, and how it "
      "works. See `--help=fields` for the {field}s and `--help=stats` for the reductions."));
  for (const Recipe& recipe : CookbookRecipes()) {
    Subsection sub{.title = std::string(recipe.task)};
    sub.children.push_back(ExampleOf(std::string(recipe.command), "sh"));
    sub.children.push_back(ProseOf(recipe.note));
    section.children.push_back(Content{.node = std::move(sub)});
  }
  return section;
}

// DESCRIPTION: the two orientation paragraphs shared by the full reference and the
// usage page.
Section DescriptionSection() {
  Section description{.title = "Description"};
  description.children.push_back(ProseOf(
      "xff walks each starting path and acts on the entries matching an expression, like `find`(1). "
      "With no path it searches the current directory; with no action it prints each match."));
  description.children.push_back(ProseOf(
      "xff has two flavors selected by the program name: invoked as `find` it is strict find (only "
      "the standard vocabulary); invoked as `xff` it enables the modern extensions. An explicit "
      "`--config=find|xff` overrides the program name. Items marked as xff extensions below are the "
      "additions over find."));
  return description;
}

// OPTIONS: the whole-run flags grouped by header. `with_details` false yields the terse
// usage-page form (summary + tags only). `audience` decides whether a flag whose build extra is
// missing carries the per-binary "NOT built into this binary" note.
Section OptionsSection(bool with_details, Audience audience = Audience::kThisBinary) {
  Section options{.title = "Options"};
  std::string_view group;
  Subsection current;
  bool have_current = false;
  for (const GlobalFlag& flag : Globals()) {
    if (flag.group != group) {
      if (have_current) {
        options.children.push_back(Content{.node = std::move(current)});
      }
      group = flag.group;
      current = Subsection{.title = std::string(flag.header)};
      have_current = true;
    }
    current.children.push_back(FlagEntry(flag, with_details, audience));
  }
  if (have_current) {
    options.children.push_back(Content{.node = std::move(current)});
  }
  return options;
}

// EXPRESSION: the primaries split into Tests / Actions / Operators. `with_details`
// false yields the terse usage-page form.
Section ExpressionSection(bool with_details) {
  Section expression{.title = "Expression"};
  for (const KindSection& kind_section : kKindSections) {
    Subsection sub{.title = std::string(kind_section.title)};
    for (const registry::Descriptor& descriptor : registry::All()) {
      if (descriptor.kind == kind_section.kind) {
        sub.children.push_back(PrimaryEntry(descriptor, with_details));
      }
    }
    expression.children.push_back(Content{.node = std::move(sub)});
  }
  return expression;
}

// The topic table rows: ALPHABETICAL (the list is long enough that SOT order reads as random), with
// each topic's informative aliases continuing the name in the term column - the same shape as flag
// aliases (`--help, -h`), so they share the term colour instead of blending into the summary prose.
// A bare plural (`archives` for `archive`) is omitted: it is guessable, lands on the same page
// anyway, and would only repeat the name - while an alias a reader cannot guess (`regex` for
// `grammars`, `env` for `environment`) is real functionality worth the width. The SOT vector keeps
// its curated order, which composes the full reference.
Rows TopicRows() {
  std::vector<HelpTopic> topics = HelpTopics();
  absl::c_sort(topics, [](const HelpTopic& lhs, const HelpTopic& rhs) { return lhs.name < rhs.name; });
  Rows rows;
  for (const HelpTopic& topic : topics) {
    std::string term(topic.name);
    for (const std::string_view alias : topic.aliases) {
      if (alias == absl::StrCat(topic.name, "s")) {
        continue;  // the guessable plural
      }
      absl::StrAppend(&term, ", ", alias);
    }
    rows.rows.push_back(Row{.term = std::move(term), .description = ParseInline(topic.summary)});
  }
  return rows;
}

// The `--help=TOPIC` index as a subsection, shared by the usage page and the guide.
Subsection TopicsSubsection() {
  Subsection topics{.title = "Topics (--help=TOPIC)"};
  topics.children.push_back(Content{.node = TopicRows()});
  return topics;
}

// HELP: the meta / doc flags and the `--help=TOPIC` index, both from their SOTs
// (HelpFlags / HelpTopics), for the usage page.
Section BuildHelpSection() {
  Section help{.title = "Help"};
  Rows flags;
  for (const HelpFlag& flag : HelpFlags()) {
    flags.rows.push_back(Row{.term = std::string(flag.display), .description = ParseInline(flag.summary)});
  }
  help.children.push_back(Content{.node = std::move(flags)});

  help.children.push_back(Content{.node = TopicsSubsection()});
  return help;
}

// The topics table alone: `--help=list` / `--help=topic` / `--help=topics` render exactly this, so
// "what can I ask for?" has an answer that is only the answer. Also embedded in the usage page and
// the `--help=help` guide via BuildHelpSection, all from the HelpTopics SOT.
Section TopicsSection() {
  // The flag spelling lives in prose, not the title: the plain backend uppercases titles, and
  // `--HELP=TOPIC` is not a thing anyone can type.
  Section section{.title = "Help topics"};
  section.children.push_back(ProseOf("Open one with `--help=TOPIC`; `--help=help` explains the help system."));
  section.children.push_back(Content{.node = TopicRows()});
  return section;
}

// The `--help=help` topic: a guide to the (subcommand-free) help system. Reuses
// BuildHelpSection (the SOT flags + topic index) with the framing prose prepended, so
// the guide can never drift from the actual help flags / topics.
Section GuideSection() {
  Section help = BuildHelpSection();
  help.children.insert(
      help.children.begin(),
      ProseOf(
          "xff has no subcommands; every kind of help is a flag. `--help` is this usage overview; "
          "`--help=NAME` documents one option or primary (e.g. `--help=-regex`, `--help=--sort`); "
          "`--help=TOPIC` opens one of the topics below; `--help=full` is the complete detailed reference; "
          "`--man` emits the roff man page and `--markdown` a Markdown reference. On a terminal this help "
          "(and `--man`) is paged per `--pager`, and `--man` is formatted like a man page, so long output "
          "scrolls instead of scrolling off; through a pipe or redirect it stays unpaged (and `--man` stays "
          "raw roff for `mandoc` / installing). `--color` and `--width` control its coloring and wrap width; "
          "see the display options below."));
  // The output globals that shape how this help itself renders, pulled from the globals SOT so the
  // guide cannot drift from the actual flags.
  Subsection display{.title = "Display options (how help is shown)"};
  Rows display_rows;
  // The display-affecting globals, in the order the section presents them.
  static constexpr std::array kDisplayFlags = std::to_array<std::string_view>({
      "--color",
      "--pager",
      "--width",
  });
  for (const std::string_view name : kDisplayFlags) {
    if (const GlobalFlag* const flag = LookupGlobal(name); flag != nullptr) {
      display_rows.rows.push_back(Row{.term = std::string(flag->display), .description = ParseInline(flag->summary)});
    }
  }
  display.children.push_back(Content{.node = std::move(display_rows)});
  help.children.push_back(Content{.node = std::move(display)});
  return help;
}

}  // namespace

Document FieldsReference() {
  Document doc;
  doc.sections.push_back(BuildFields());
  return doc;
}

Document BuildUsage() {
  Document doc{
      .name = "xff",
      .tagline = "eXtended File Find, a find(1)-compatible file finder with modern extensions",
      .usage = "[option...] [path...] [expression]",
  };
  doc.sections.push_back(DescriptionSection());
  doc.sections.push_back(OptionsSection(/*with_details=*/false));
  doc.sections.push_back(ExpressionSection(/*with_details=*/false));
  doc.sections.push_back(BuildHelpSection());
  return doc;
}

std::optional<Document> IndexReference(std::string_view name) {
  Document doc;
  if (name == "list" || name == "topic" || name == "topics") {
    // The list OF TOPICS, not the usage page it used to alias: `list`'s table row promised an index
    // and delivered plain --help, which reads as "does not work".
    doc.sections.push_back(TopicsSection());
    return doc;
  }
  if (name == "all") {
    // Every option + primary, summaries only (no detail blocks).
    doc.sections.push_back(OptionsSection(/*with_details=*/false));
    doc.sections.push_back(ExpressionSection(/*with_details=*/false));
  } else if (name == "expressions") {
    // The expression vocabulary (summaries), without the whole-run global flags.
    doc.sections.push_back(ExpressionSection(/*with_details=*/false));
  } else {
    return std::nullopt;
  }
  return doc;
}

std::optional<Document> TopicReference(std::string_view name) {
  Document doc;
  if (name == "fields") {
    doc.sections.push_back(BuildFields());
  } else if (name == "printf") {
    doc.sections.push_back(PrintfSection());
  } else if (name == "time") {
    doc.sections.push_back(TimeSection());
  } else if (name == "size") {
    doc.sections.push_back(SizeSection());
  } else if (name == "grammars" || name == "regex" || name == "regexp") {
    doc.sections.push_back(GrammarsSection());
  } else if (name == "content") {
    doc.sections.push_back(ContentSection(/*in_full=*/false));
  } else if (name == "archive" || name == "archives") {
    doc.sections.push_back(ArchiveSection(/*in_full=*/false));
  } else if (name == "stats") {
    doc.sections.push_back(StatsSection(/*in_full=*/false));
  } else if (name == "config") {
    doc.sections.push_back(ConfigSection(/*in_full=*/false));
  } else if (name == "environment" || name == "env") {
    doc.sections.push_back(EnvironmentSection());
  } else if (name == "cookbook" || name == "examples" || name == "recipes") {
    doc.sections.push_back(BuildExamples());
  } else if (name == "notice" || name == "notices") {
    doc.sections.push_back(NoticeSection());
  } else if (name == "license" || name == "licenses") {
    doc.sections.push_back(LicenseSection());
  } else if (name == "help") {
    doc.sections.push_back(GuideSection());
  } else {
    return std::nullopt;
  }
  return doc;
}

std::optional<Document> EntryReference(std::string_view name) {
  // An expression primary / operator / action (leading-dash convenience: regex -> -regex).
  const registry::Descriptor* descriptor = registry::Lookup(name);
  if (descriptor == nullptr && !name.empty() && name.front() != '-' && name.front() != '!') {
    descriptor = registry::Lookup(absl::StrCat("-", name));
  }
  // Otherwise a whole-run global flag (leading-dashes convenience: sort -> --sort).
  const GlobalFlag* flag = nullptr;
  if (descriptor == nullptr) {
    flag = LookupGlobal(name);
    if (flag == nullptr && !name.empty() && name.front() != '-') {
      flag = LookupGlobal(absl::StrCat("--", name));
    }
  }
  if (descriptor == nullptr && flag == nullptr) {
    return std::nullopt;
  }
  // A title-less section: the single entry renders without a section heading.
  Section section;
  section.children.push_back(descriptor != nullptr ? PrimaryEntry(*descriptor) : FlagEntry(*flag));
  Document doc;
  doc.sections.push_back(std::move(section));
  return doc;
}

Document BuildReference(Audience audience) {
  Document doc{
      .name = "xff",
      .tagline = "eXtended File Find, a find(1)-compatible file finder with modern extensions",
      .usage = "[option...] [path...] [expression]",
  };

  doc.sections.push_back(DescriptionSection());
  doc.sections.push_back(ConfigSection(/*in_full=*/true));
  doc.sections.push_back(OptionsSection(/*with_details=*/true, audience));
  doc.sections.push_back(ExpressionSection(/*with_details=*/true));

  doc.sections.push_back(BuildFields());
  doc.sections.push_back(PrintfSection());
  doc.sections.push_back(TimeSection());
  doc.sections.push_back(SizeSection());
  doc.sections.push_back(GrammarsSection());
  doc.sections.push_back(ContentSection(/*in_full=*/true));
  doc.sections.push_back(ArchiveSection(/*in_full=*/true));
  doc.sections.push_back(StatsSection(/*in_full=*/true));
  doc.sections.push_back(EnvironmentSection());
  doc.sections.push_back(BuildExamples());

  Section exit_status{.title = "Exit status"};
  exit_status.children.push_back(ProseOf(
      "0 on success, 2 on error. With `--quiet` or `--exit-match` the exit is 0 when something "
      "matched and 1 when nothing did (an error still outranks the match status)."));
  doc.sections.push_back(std::move(exit_status));

  Section see_also{.title = "See also"};
  SeeAlso block{
      .refs =
          {{.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"},
           {.kind = RefTarget::Kind::kManPage, .id = "grep", .section = "1"},
           {.kind = RefTarget::Kind::kManPage, .id = "fnmatch", .section = "3"},
           {.kind = RefTarget::Kind::kManPage, .id = "glob", .section = "7"},
           {.kind = RefTarget::Kind::kManPage, .id = "pcre2pattern", .section = "3"}},
      .note = ParseInline(
          "For the `--regextype` grammars see the Regex grammars section above (`--help=grammars`). FNMATCH "
          "is the platform's fnmatch(3) and PCRE2 is pcre2pattern(3); GLOB and SHGLOB are xff's own "
          "path-aware globs (compiled to RE2), NOT POSIX glob(7) - that page is listed only as background "
          "on shell globbing. The default RE2 grammar has no man page; its syntax is at "
          "https://github.com/google/re2/wiki/Syntax ."),
  };
  see_also.children.push_back(Content{.node = std::move(block)});
  doc.sections.push_back(std::move(see_also));

  return doc;
}

}  // namespace xff::cli
