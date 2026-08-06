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

// A definition entry for a global flag: its display, summary, and detail blocks,
// enriched with the (global, xff|find) tags, a "not built into this binary" note
// when its build extra is absent, and the Affects / Affected-by influence blocks.
Content FlagEntry(const GlobalFlag& flag) {
  Blocks details;
  if (!flag.extra.empty() && !ExtraEnabled(flag.extra)) {
    details.push_back(ProseOf(
        absl::StrCat(
            "Not built into this binary: rebuild with `--//xff:", flag.extra, "` (used as-is, it is a hard error).")));
  }
  for (Content& block : ParseBlocks(flag.details)) {
    details.push_back(std::move(block));
  }
  AppendInfluence(&details, "Affects:", absl::StrSplit(flag.affects, ',', absl::SkipEmpty()));
  AppendInfluence(&details, "Affected by:", AffectedByFlags(flag.name));
  return Content{
      .node = Entry{
          .term = std::string(flag.display),
          .summary = ParseInline(flag.summary),
          .details = std::move(details),
          .xff = flag.xff,
          .tags = FlagTags(flag),
      }};
}

// A definition entry for an expression primary: its synopsis, summary, and detail
// blocks, enriched with the (kind, xff|find, [safety]) tags and the Affected-by block.
Content PrimaryEntry(const registry::Descriptor& descriptor) {
  Blocks details = ParseBlocks(descriptor.details);
  AppendInfluence(&details, "Affected by:", AffectedByFlags(descriptor.name));
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

}  // namespace

Document FieldsReference() {
  Document doc;
  doc.sections.push_back(BuildFields());
  return doc;
}

Document BuildReference() {
  Document doc{
      .name = "xff",
      .tagline = "eXtended File Find, a find(1)-compatible file finder with modern extensions",
      .usage = "[option...] [path...] [expression]",
  };

  Section description{.title = "Description"};
  description.children.push_back(ProseOf(
      "xff walks each starting path and acts on the entries matching an expression, like `find`(1). "
      "With no path it searches the current directory; with no action it prints each match."));
  description.children.push_back(ProseOf(
      "xff has two flavors selected by the program name: invoked as `find` it is strict find (only "
      "the standard vocabulary); invoked as `xff` it enables the modern extensions. An explicit "
      "`--config=find|xff` overrides the program name. Items marked as xff extensions below are the "
      "additions over find."));
  doc.sections.push_back(std::move(description));

  Section options{.title = "Options"};
  {
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
      current.children.push_back(FlagEntry(flag));
    }
    if (have_current) {
      options.children.push_back(Content{.node = std::move(current)});
    }
  }
  doc.sections.push_back(std::move(options));

  Section expression{.title = "Expression"};
  for (const KindSection& kind_section : kKindSections) {
    Subsection sub{.title = std::string(kind_section.title)};
    for (const registry::Descriptor& descriptor : registry::All()) {
      if (descriptor.kind == kind_section.kind) {
        sub.children.push_back(PrimaryEntry(descriptor));
      }
    }
    expression.children.push_back(Content{.node = std::move(sub)});
  }
  doc.sections.push_back(std::move(expression));

  doc.sections.push_back(BuildFields());

  doc.sections.push_back(VocabSection(
      "Printf directives", "Directives for -printf / -fprintf / -println FORMAT, and the `%{field}` escape.",
      engine::PrintfDocs()));
  doc.sections.push_back(VocabSection(
      "Time formats", "Presets and strftime patterns for --time-format, --timezone, and time-field {:qualifiers}.",
      datetime::FormatDocs()));
  doc.sections.push_back(VocabSection("Size units", "Units for -size / -blocks [+|-]N[unit].", engine::SizeUnitDocs()));
  doc.sections.push_back(VocabSection(
      "Regex grammars",
      "The grammar for -regex / -iregex and the content matchers -rxc / -grep, chosen by `--regextype` "
      "(default RE2). EXACT, FNMATCH, GLOB and SHGLOB are core engines, always built in; PCRE2 is a "
      "build-time extra (see `--help=extras`). RE2 and PCRE2 have canonical external references, so the "
      "smaller engines are spelled out in full here: they have no single authoritative man page, and "
      "FNMATCH delegates to the platform's fnmatch(3), whose class / collation details vary by system.",
      regex::GrammarDocs()));

  Section examples{.title = "Examples"};
  examples.children.push_back(ExampleOf(RenderHelp("cookbook").value_or("")));
  doc.sections.push_back(std::move(examples));

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
