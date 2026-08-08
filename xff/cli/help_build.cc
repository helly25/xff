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

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/types/span.h"
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

// A definition entry for a global flag: its display, summary, and (when `with_details`)
// its detail blocks enriched with a "not built into this binary" note when its build
// extra is absent and the Affects / Affected-by influence blocks. `with_details` is
// false for the terse usage page (summary + tags only).
Content FlagEntry(const GlobalFlag& flag, bool with_details = true) {
  Blocks details;
  // The not-built note shows even on the terse usage page: a flag whose build extra is
  // absent is a hard error if used, so the reader must see it up front.
  if (!flag.extra.empty() && !ExtraEnabled(flag.extra)) {
    details.push_back(ProseOf(
        absl::StrCat(
            "NOT built into this binary: rebuild with `--//xff:", flag.extra, "` (used as-is, it is a hard error).")));
  }
  if (with_details) {
    // The allowed-value table (for a flag whose synopsis collapsed a value grammar to a
    // `<PLACEHOLDER>`) leads, as an aligned wrapping `value  meaning` list, before the prose.
    if (!flag.values.empty()) {
      Rows rows;
      rows.rows.reserve(flag.values.size());
      for (const ValueDoc& value : flag.values) {
        rows.rows.push_back(Row{.term = std::string(value.value), .description = ParseInline(value.meaning)});
      }
      details.push_back(Content{.node = std::move(rows)});
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
  return VocabSection(
      "Time formats", "Presets and strftime patterns for --time-format, --timezone, and time-field {:qualifiers}.",
      datetime::FormatDocs());
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

// STATISTICS: the two terminal reductions (--summary / --histogram). The flags are
// pulled from the globals SOT via the "stats" topic tag so the list cannot drift, then
// worked examples. Standalone as `--help=stats` (see TopicReference) and folded into
// the full reference.
Section StatsSection() {
  Section section{.title = "Statistics"};
  section.children.push_back(ProseOf(
      "xff statistics reductions. `--summary` and `--histogram` replace the per-match listing with an "
      "aggregate over all matches; they are independent and combinable (one walk feeds both), and an "
      "explicit action (`-print` / `-exec`) still runs. `--format=jsonl` emits machine rows instead."));
  for (const GlobalFlag& flag : Globals()) {
    if (flag.topic == "stats") {
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

// CONFIGURATION: how options resolve (layered tiers + the command line), how a style is
// chosen (--config / argv[0]), and how dangerous --xffrc directives are armed. The flags
// are pulled from the globals SOT via the "config" topic tag so the list cannot drift;
// the layering / argv[0] / arming rules are prose. Standalone as `--help=config` (see
// TopicReference) and folded into the full reference.
Section ConfigSection() {
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

  Subsection flags{.title = "Config flags"};
  for (const GlobalFlag& flag : Globals()) {
    if (flag.topic == "config") {
      flags.children.push_back(FlagEntry(flag));
    }
  }
  section.children.push_back(Content{.node = std::move(flags)});
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
// usage-page form (summary + tags only).
Section OptionsSection(bool with_details) {
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
    current.children.push_back(FlagEntry(flag, with_details));
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

// HELP: the meta / doc flags and the `--help=TOPIC` index, both from their SOTs
// (HelpFlags / HelpTopics), for the usage page.
Section BuildHelpSection() {
  Section help{.title = "Help"};
  Rows flags;
  for (const HelpFlag& flag : HelpFlags()) {
    flags.rows.push_back(Row{.term = std::string(flag.display), .description = ParseInline(flag.summary)});
  }
  help.children.push_back(Content{.node = std::move(flags)});

  Subsection topics{.title = "Topics (--help=TOPIC)"};
  Rows topic_rows;
  for (const HelpTopic& topic : HelpTopics()) {
    topic_rows.rows.push_back(Row{.term = std::string(topic.name), .description = ParseInline(topic.summary)});
  }
  topics.children.push_back(Content{.node = std::move(topic_rows)});
  help.children.push_back(Content{.node = std::move(topics)});
  return help;
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
          "`--man` emits the roff man page and `--markdown` a Markdown reference."));
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
  if (name == "list") {
    return BuildUsage();  // the whole-vocabulary index is the usage page
  }
  Document doc;
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
  } else if (name == "grammars") {
    doc.sections.push_back(GrammarsSection());
  } else if (name == "stats") {
    doc.sections.push_back(StatsSection());
  } else if (name == "config") {
    doc.sections.push_back(ConfigSection());
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

Document BuildReference() {
  Document doc{
      .name = "xff",
      .tagline = "eXtended File Find, a find(1)-compatible file finder with modern extensions",
      .usage = "[option...] [path...] [expression]",
  };

  doc.sections.push_back(DescriptionSection());
  doc.sections.push_back(ConfigSection());
  doc.sections.push_back(OptionsSection(/*with_details=*/true));
  doc.sections.push_back(ExpressionSection(/*with_details=*/true));

  doc.sections.push_back(BuildFields());
  doc.sections.push_back(PrintfSection());
  doc.sections.push_back(TimeSection());
  doc.sections.push_back(SizeSection());
  doc.sections.push_back(GrammarsSection());
  doc.sections.push_back(StatsSection());
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
