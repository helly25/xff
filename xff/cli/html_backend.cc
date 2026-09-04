// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/cli/html_backend.h"

#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "xff/cli/help_model.h"

namespace xff::cli {
namespace {

std::string Tags(const Entry& entry) {
  if (!entry.tags.empty()) {
    return absl::StrCat("<span class=\"tags\">(", HtmlEscape(absl::StrJoin(entry.tags, ", ")), ")</span>");
  }
  return entry.xff ? "<span class=\"tags\">(xff)</span>" : std::string();
}

}  // namespace

std::string HtmlBackend::UniqueAnchor(std::string_view explicit_anchor, std::string_view fallback) {
  std::string anchor = HtmlSlug(explicit_anchor.empty() ? fallback : explicit_anchor);
  const std::size_t occurrence = anchor_counts_[anchor]++;
  if (occurrence != 0) {
    absl::StrAppend(&anchor, "-", occurrence);
  }
  return HtmlAttributeEscape(anchor);
}

std::string HtmlEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char chr : text) {
    switch (chr) {
      case '&': absl::StrAppend(&out, "&amp;"); break;
      case '<': absl::StrAppend(&out, "&lt;"); break;
      case '>': absl::StrAppend(&out, "&gt;"); break;
      default: out.push_back(chr); break;
    }
  }
  return out;
}

std::string HtmlAttributeEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char chr : text) {
    switch (chr) {
      case '&': absl::StrAppend(&out, "&amp;"); break;
      case '<': absl::StrAppend(&out, "&lt;"); break;
      case '>': absl::StrAppend(&out, "&gt;"); break;
      case '"': absl::StrAppend(&out, "&quot;"); break;
      case '\'': absl::StrAppend(&out, "&#39;"); break;
      default: out.push_back(chr); break;
    }
  }
  return out;
}

std::string HtmlSlug(std::string_view text) {
  std::string slug;
  bool pending_dash = false;
  for (const char chr : text) {
    if (absl::ascii_isalnum(chr)) {
      if (pending_dash && !slug.empty()) {
        slug.push_back('-');
      }
      pending_dash = false;
      slug.push_back(absl::ascii_tolower(chr));
    } else {
      pending_dash = true;
    }
  }
  if (slug.empty()) {
    slug = "item";
    for (const unsigned char chr : text) {
      absl::StrAppend(&slug, "-", absl::StrFormat("%02x", chr));
    }
  }
  return slug;
}

std::string HtmlRefLink(const RefTarget& target, std::string_view label) {
  std::string text = HtmlEscape(label.empty() ? target.id : label);
  switch (target.kind) {
    case RefTarget::Kind::kUrl: return absl::StrCat("<a href=\"", HtmlAttributeEscape(target.id), "\">", text, "</a>");
    case RefTarget::Kind::kManPage: {
      const std::string locator =
          label.empty() ? absl::StrCat(target.id, "(", target.section, ")") : std::string(label);
      return absl::StrCat("<cite>", HtmlEscape(locator), "</cite>");
    }
    case RefTarget::Kind::kTopic:
      return absl::StrCat("<a href=\"#", HtmlAttributeEscape(HtmlSlug(target.id)), "\">", text, "</a>");
    case RefTarget::Kind::kFlag:
      return absl::StrCat("<a href=\"#flag-", HtmlAttributeEscape(HtmlSlug(target.id)), "\">", text, "</a>");
    case RefTarget::Kind::kPrimary:
      return absl::StrCat("<a href=\"#primary-", HtmlAttributeEscape(HtmlSlug(target.id)), "\">", text, "</a>");
    case RefTarget::Kind::kAnchor:
      return absl::StrCat("<a href=\"#", HtmlAttributeEscape(HtmlSlug(target.id)), "\">", text, "</a>");
  }
  return text;
}

std::string RenderInlinesHtml(const Inlines& runs) {
  std::string out;
  for (const Inline& run : runs) {
    const std::string text = HtmlEscape(run.text);
    switch (run.style) {
      case Inline::Style::kText: absl::StrAppend(&out, text); break;
      case Inline::Style::kCode: absl::StrAppend(&out, "<code>", text, "</code>"); break;
      case Inline::Style::kEmphasis: absl::StrAppend(&out, "<em>", text, "</em>"); break;
      case Inline::Style::kStrong: absl::StrAppend(&out, "<strong>", text, "</strong>"); break;
      case Inline::Style::kRef:
        absl::StrAppend(&out, run.target.has_value() ? HtmlRefLink(*run.target, run.text) : text);
        break;
    }
  }
  return out;
}

void HtmlBackend::Preamble(const Document& doc) {
  absl::StrAppend(
      &out_, "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n",
      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>", HtmlEscape(doc.name),
      " reference</title>\n<style>\n",
      ":root{color-scheme:light "
      "dark;--bg:#fff;--fg:#202124;--muted:#5f6368;--rule:#d0d7de;--code:#f3f4f6;--link:#0969da}",
      "@media(prefers-color-scheme:dark){:root{--bg:#0d1117;--fg:#e6edf3;--muted:#9da7b3;--rule:#30363d;--code:#161b22;"
      "--link:#58a6ff}}",
      "*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;background:var(--bg);color:var(--fg);font:"
      "16px/1.55 system-ui,-apple-system,BlinkMacSystemFont,\"Segoe UI\",sans-serif}",
      "main,header{width:min(100% - 2rem,72rem);margin-inline:auto}header{padding:3rem 0 1.5rem;border-bottom:1px "
      "solid var(--rule)}",
      "main{padding:1rem 0 "
      "4rem}h1,h2,h3,h4{line-height:1.25;scroll-margin-top:1rem}h1{margin:0;font-size:2.5rem}h2{margin-top:2.5rem;"
      "padding-bottom:.35rem;border-bottom:1px solid var(--rule)}",
      "h3{margin-top:2rem}h4{margin:.2rem "
      "0}.tagline,.tags{color:var(--muted)}.usage{font-size:1.05rem}.entry{padding:.65rem 0}.summary{margin:.25rem "
      "0}.tags{margin-left:.5rem;font-weight:400;font-size:.9rem}",
      "code,pre{font-family:ui-monospace,SFMono-Regular,Consolas,monospace}code{padding:.12rem "
      ".3rem;border-radius:.25rem;background:var(--code)}pre{overflow:auto;padding:1rem;border:1px solid "
      "var(--rule);border-radius:.4rem;background:var(--code)}pre code{padding:0;background:none}",
      "a{color:var(--link)}dl.rows{display:grid;grid-template-columns:minmax(10rem,max-content) 1fr;gap:.5rem "
      "1rem}dt{font-weight:600}dd{margin:0}table{display:block;overflow:auto;border-collapse:collapse;margin:1rem "
      "0}th,td{padding:.45rem .7rem;border:1px solid "
      "var(--rule);text-align:left;vertical-align:top}th{background:var(--code)}",
      "@media(max-width:42rem){dl.rows{display:block}dd{margin:.15rem 0 .5rem}.tags{display:block;margin:.2rem 0 0}}\n",
      "</style>\n</head>\n<body>\n<header>\n<h1>", HtmlEscape(doc.name), "</h1>\n<p class=\"tagline\">",
      HtmlEscape(doc.tagline), ".</p>\n<p class=\"usage\"><strong>Usage:</strong> <code>", HtmlEscape(doc.name), " ",
      HtmlEscape(doc.usage), "</code></p>\n</header>\n<main>\n");
}

void HtmlBackend::BeginSection(const Section& section) {
  absl::StrAppend(
      &out_, "<section id=\"", UniqueAnchor(section.anchor, section.title), "\">\n<h2>", HtmlEscape(section.title),
      "</h2>\n");
}

void HtmlBackend::EndSection(const Section& /*section*/) {
  absl::StrAppend(&out_, "</section>\n");
}

void HtmlBackend::BeginSubsection(const Subsection& subsection) {
  if (subsection.title.empty()) {
    absl::StrAppend(&out_, "<div class=\"group\">\n");
    return;
  }
  absl::StrAppend(
      &out_, "<section id=\"", UniqueAnchor(subsection.anchor, subsection.title), "\">\n<h3>",
      HtmlEscape(subsection.title), "</h3>\n");
}

void HtmlBackend::EndSubsection(const Subsection& subsection) {
  absl::StrAppend(&out_, subsection.title.empty() ? "</div>\n" : "</section>\n");
}

void HtmlBackend::BeginEntry(const Entry& entry) {
  absl::StrAppend(
      &out_, R"(<article class="entry" id=")", UniqueAnchor(entry.anchor, entry.term), "\">\n<h4><code>",
      HtmlEscape(entry.term), "</code>", Tags(entry), "</h4>\n<p class=\"summary\">", RenderInlinesHtml(entry.summary),
      "</p>\n");
}

void HtmlBackend::EndEntry(const Entry& /*entry*/) {
  absl::StrAppend(&out_, "</article>\n");
}

void HtmlBackend::EmitProse(const Prose& prose) {
  absl::StrAppend(&out_, "<p>", RenderInlinesHtml(prose.runs), "</p>\n");
}

void HtmlBackend::EmitExample(const Example& example) {
  const std::string language = example.lang.empty()
                                   ? std::string()
                                   : absl::StrCat(" class=\"language-", HtmlAttributeEscape(example.lang), "\"");
  absl::StrAppend(&out_, "<pre><code", language, ">", HtmlEscape(example.text), "</code></pre>\n");
}

void HtmlBackend::EmitBullets(const Bullets& bullets) {
  absl::StrAppend(&out_, "<ul>\n");
  for (const Inlines& item : bullets.items) {
    absl::StrAppend(&out_, "<li>", RenderInlinesHtml(item), "</li>\n");
  }
  absl::StrAppend(&out_, "</ul>\n");
}

void HtmlBackend::EmitRows(const Rows& rows) {
  absl::StrAppend(&out_, "<dl class=\"rows\">\n");
  for (const Row& row : rows.rows) {
    absl::StrAppend(
        &out_, "<dt><code>", HtmlEscape(row.term), "</code></dt><dd>", RenderInlinesHtml(row.description), "</dd>\n");
  }
  absl::StrAppend(&out_, "</dl>\n");
}

void HtmlBackend::EmitTable(const Table& table) {
  absl::StrAppend(&out_, "<table>\n<thead><tr>");
  for (const std::string& cell : table.header) {
    absl::StrAppend(&out_, "<th scope=\"col\">", HtmlEscape(cell), "</th>");
  }
  absl::StrAppend(&out_, "</tr></thead>\n<tbody>\n");
  for (const std::vector<std::string>& row : table.cells) {
    absl::StrAppend(&out_, "<tr>");
    for (const std::string& cell : row) {
      absl::StrAppend(&out_, "<td>", HtmlEscape(cell), "</td>");
    }
    absl::StrAppend(&out_, "</tr>\n");
  }
  absl::StrAppend(&out_, "</tbody>\n</table>\n");
}

void HtmlBackend::EmitSeeAlso(const SeeAlso& see_also) {
  absl::StrAppend(&out_, "<p class=\"see-also\">");
  for (std::size_t i = 0; i < see_also.refs.size(); ++i) {
    if (i != 0) {
      absl::StrAppend(&out_, ", ");
    }
    absl::StrAppend(&out_, HtmlRefLink(see_also.refs[i], {}));
  }
  absl::StrAppend(&out_, "</p>\n");
  if (!see_also.note.empty()) {
    absl::StrAppend(&out_, "<p>", RenderInlinesHtml(see_also.note), "</p>\n");
  }
}

std::string HtmlBackend::Take() {
  absl::StrAppend(&out_, "</main>\n</body>\n</html>\n");
  return std::move(out_);
}

}  // namespace xff::cli
