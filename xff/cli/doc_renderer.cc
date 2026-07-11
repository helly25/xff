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

#include "xff/cli/doc_renderer.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "xff/cli/globals.h"
#include "xff/cli/help.h"
#include "xff/datetime/datetime.h"
#include "xff/engine/evaluate.h"
#include "xff/fields/fields.h"
#include "xff/regex/regex.h"
#include "xff/registry/descriptor.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

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

// The dynamic {field} namespaces (FIELDS > Dynamic namespaces), a term/description list.
constexpr std::array<DocRow, 4> kDynamicNamespaces = {{
    {"{0}..{N}", "-regex captures ({0} the whole match, {1}..{N} the groups)"},
    {"{env.NAME}", "a process environment variable"},
    {"{def.NAME}", "a --define value"},
    {"{capture.NAME}", "a -capture command result"},
}};

// The FIELDS > FIELDS section renders the named {field} vocabulary as grouped subsections of
// aligned rows, then the brace rules, dynamic namespaces, and qualifiers. The composed
// "{name} {alias}" terms and the {path:COMP} description are held in local storage (reserved up
// front so their string_views never dangle) for the duration of the Rows() spans they back.
void WriteFields(DocRenderer& out) {
  out.Section("Fields");
  out.Prose(
      "The `{field}` placeholder vocabulary, substituted per entry in --template / --format, in "
      "-printf via the `%{field}` escape, and (with --exec-fields) in -exec.");

  const std::vector<fields::FieldDoc> docs = fields::FieldDocs();
  std::vector<std::string> term_store;
  term_store.reserve(docs.size());
  for (const fields::FieldDoc& doc : docs) {
    std::string term = absl::StrCat("{", doc.name, "}");
    for (const std::string_view alias : doc.aliases) {
      absl::StrAppend(&term, " {", alias, "}");
    }
    term_store.emplace_back(std::move(term));
  }
  std::string_view group;
  std::vector<DocRow> rows;
  for (std::size_t i = 0; i < docs.size(); ++i) {
    if (docs[i].group != group) {
      if (!rows.empty()) {
        out.Rows(rows);
        rows.clear();
      }
      group = docs[i].group;
      out.Subsection(docs[i].header);
    }
    rows.emplace_back(term_store[i], docs[i].summary);
  }
  if (!rows.empty()) {
    out.Rows(rows);
  }

  out.Subsection("Braces");
  WriteMarkdown(
      out,
      "- `{{` and `}}` emit literal braces\n"
      "- `{}` is an alias for `{path}`\n"
      "- an unknown field renders empty\n"
      "- a malformed or unterminated `{` stays literal");

  out.Subsection("Dynamic namespaces");
  out.Rows(kDynamicNamespaces);

  const std::string path_comp = absl::StrCat(
      "path component of the value: ", absl::StrJoin(fields::PathComponentKeywords(), "|"),
      "; any path-valued field composes, e.g. {relpath:stem}, {def.B:dir}");
  const std::array<DocRow, 6> qualifiers = {{
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
  out.Subsection("Qualifiers ({field:QUAL})");
  out.Rows(qualifiers);
  out.Prose(
      "An m// extraction is a left-to-right pipeline: s/// maps whatever is flowing (each line, then "
      "the scalar), and a terminal reducer such as join collapses the stream to one scalar.");
  // Hand-aligned ASCII span diagram (ranges under each stage). Rendered verbatim (Example -> a
  // markdown code fence / roff .nf), so the alignment survives; uses only | _ / ( ) so the Roff()
  // escaper and mandoc leave it intact. Kept in sync with RenderFields in help.cc (--help=fields).
  out.Example(
      "  {cap:m/PAT/REP/;s/PAT/REP/;join(SEP);s/PAT/REP/}\n"
      "       |________| |________| |_______| |________|\n"
      "       extract    map each   reduce    rewrite\n"
      "       per line   line       stream    scalar");
  out.Prose(
      "For -printf's own % directives (%p %f %s %t ...) and the `%{field}` escape that bridges them "
      "to this vocabulary, see the Printf directives section below.");
}

}  // namespace

void WriteMarkdown(DocRenderer& out, std::string_view block) {
  std::vector<std::string_view> bullets;
  std::string paragraph;
  const auto flush_paragraph = [&] {
    if (!paragraph.empty()) {
      out.Prose(paragraph);
      paragraph.clear();
    }
  };
  const auto flush_bullets = [&] {
    if (!bullets.empty()) {
      out.Bullets(bullets);
      bullets.clear();
    }
  };
  for (const std::string_view raw : absl::StrSplit(block, '\n')) {
    const std::string_view line = absl::StripAsciiWhitespace(raw);
    std::size_t level = 0;
    while (level < line.size() && line[level] == '#') {
      ++level;
    }
    if (level > 0 && level < line.size() && line[level] == ' ') {
      // A heading line (`# ` .. `### `): level 1 is a Section, deeper is a Subsection (the two
      // heading levels the renderers model). Flush any open paragraph / list first.
      flush_paragraph();
      flush_bullets();
      const std::string_view title = absl::StripAsciiWhitespace(line.substr(level + 1));
      if (level == 1) {
        out.Section(title);
      } else {
        out.Subsection(title);
      }
    } else if (absl::StartsWith(line, "- ") || absl::StartsWith(line, "* ")) {
      flush_paragraph();
      bullets.push_back(absl::StripAsciiWhitespace(line.substr(2)));
    } else if (line.empty()) {
      flush_paragraph();
      flush_bullets();
    } else {
      flush_bullets();
      if (!paragraph.empty()) {
        paragraph += ' ';
      }
      absl::StrAppend(&paragraph, line);
    }
  }
  flush_paragraph();
  flush_bullets();
}

void WriteReference(DocRenderer& out) {
  out.Document(
      "xff", "eXtended File Find, a find(1)-compatible file finder with modern extensions",
      "[option...] [path...] [expression]");

  out.Section("Description");
  out.Prose(
      "xff walks each starting path and acts on the entries matching an expression, like `find`(1). "
      "With no path it searches the current directory; with no action it prints each match.");
  out.Prose(
      "xff has two flavors selected by the program name: invoked as `find` it is strict find (only "
      "the standard vocabulary); invoked as `xff` it enables the modern extensions. An explicit "
      "`--config=find|xff` overrides the program name. Items marked as xff extensions below are the "
      "additions over find.");

  out.Section("Options");
  std::string_view group;
  for (const GlobalFlag& flag : Globals()) {
    if (flag.group != group) {
      group = flag.group;
      out.Subsection(flag.header);
    }
    out.Entry(flag.display, flag.summary, flag.details, flag.xff);
  }

  out.Section("Expression");
  for (const KindSection& section : kKindSections) {
    out.Subsection(section.title);
    for (const registry::Descriptor& descriptor : registry::All()) {
      if (descriptor.kind == section.kind) {
        out.Entry(
            absl::StrCat(descriptor.name, ArgHint(descriptor)), descriptor.summary, descriptor.details,
            descriptor.style == registry::Style::kXff);
      }
    }
  }

  WriteFields(out);

  out.Section("Printf directives");
  out.Prose("Directives for -printf / -fprintf / -println FORMAT, and the `%{field}` escape.");
  out.Rows(engine::PrintfDocs());

  out.Section("Time formats");
  out.Prose("Presets and strftime patterns for --time-format, --timezone, and time-field {:qualifiers}.");
  out.Rows(datetime::FormatDocs());

  out.Section("Size units");
  out.Prose("Units for -size / -blocks [+|-]N[unit].");
  out.Rows(engine::SizeUnitDocs());

  out.Section("Regex grammars");
  out.Prose(
      "The grammar for -regex / -iregex and the content matchers -rxc / -grep, chosen by `--regextype` "
      "(default RE2). EXACT, FNMATCH, GLOB and SHGLOB are core engines, always built in; PCRE2 is a "
      "build-time extra (see `--help=extras`). RE2 and PCRE2 have canonical external references, so the "
      "smaller engines are spelled out in full here: they have no single authoritative man page, and "
      "FNMATCH delegates to the platform's fnmatch(3), whose class / collation details vary by system.");
  out.Rows(regex::GrammarDocs());

  out.Section("Examples");
  out.Example(RenderHelp("cookbook").value_or(""));

  out.Section("Exit status");
  out.Prose(
      "0 on success, 2 on error. With `--quiet` or `--exit-match` the exit is 0 when something "
      "matched and 1 when nothing did (an error still outranks the match status).");

  out.Section("See also");
  static constexpr std::array<CrossRef, 5> kSeeAlso = {{
      {.name = "find", .section = "1"},
      {.name = "grep", .section = "1"},
      {.name = "fnmatch", .section = "3"},
      {.name = "glob", .section = "7"},
      {.name = "pcre2pattern", .section = "3"},
  }};
  out.SeeAlso(
      kSeeAlso,
      "For the `--regextype` grammars see the Regex grammars section above (`--help=grammars`). FNMATCH "
      "is the platform's fnmatch(3) and PCRE2 is pcre2pattern(3); GLOB and SHGLOB are xff's own "
      "path-aware globs (compiled to RE2), NOT POSIX glob(7) - that page is listed only as background "
      "on shell globbing. The default RE2 grammar has no man page; its syntax is at "
      "https://github.com/google/re2/wiki/Syntax .");
}

}  // namespace xff::cli
