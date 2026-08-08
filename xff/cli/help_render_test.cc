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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_split.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/cli/globals.h"
#include "xff/cli/help.h"
#include "xff/cli/help_backend.h"
#include "xff/cli/help_build.h"
#include "xff/cli/help_model.h"
#include "xff/cli/plain_backend.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

// Renders the single-entry `--help=NAME` help from the model (the same path the CLI
// uses), or "" when NAME is not an entry. Single-flag / single-primary help moved off
// RenderHelp onto EntryReference + the plain backend, so these tests exercise that.
std::string RenderEntry(std::string_view name) {
  const std::optional<Document> doc = EntryReference(name);
  if (!doc.has_value()) {
    return "";
  }
  PlainTextBackend backend;
  RenderDocument(*doc, backend);
  return backend.Take();
}

// Renders a `--help=TOPIC` topic from the model (TopicReference + the plain backend),
// the same path the CLI uses, or "" when NAME is not a model topic. Sub-vocabulary and
// prose topics (fields / stats / ...) render this way rather than through RenderHelp.
std::string RenderTopicDoc(std::string_view name) {
  const std::optional<Document> doc = TopicReference(name);
  if (!doc.has_value()) {
    return "";
  }
  PlainTextBackend backend;
  RenderDocument(*doc, backend);
  return backend.Take();
}

// Renders a `--help=list` / `--help=all` / `--help=expressions` index document (IndexReference +
// the plain backend), the same path the CLI uses, or "" when NAME is not an index. Keeps the
// optional behind an if-guard so callers work with the rendered string directly.
std::string RenderIndex(std::string_view name) {
  const std::optional<Document> doc = IndexReference(name);
  if (!doc.has_value()) {
    return "";
  }
  PlainTextBackend backend;
  RenderDocument(*doc, backend);
  return backend.Take();
}

// Renders any model Document to plain text (the CLI path), for the index / full-reference
// drift guards below.
std::string RenderDoc(const Document& doc) {
  PlainTextBackend backend;
  RenderDocument(doc, backend);
  return backend.Take();
}

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Lt;
using ::testing::Not;
using ::testing::SizeIs;

struct HelpTest : ::testing::Test {};

TEST_F(HelpTest, TopicRendersNameSummaryAndTags) {
  EXPECT_THAT(
      RenderEntry("-regex"), AllOf(
                                 HasSubstr("-regex"), HasSubstr("regular expression"),  // the summary
                                 HasSubstr("test"), HasSubstr("find")));                // kind + style tags
}

TEST_F(HelpTest, DashlessTopicResolves) {
  // Friendly: `--help=regex` finds -regex.
  EXPECT_THAT(RenderEntry("regex"), HasSubstr("-regex"));
}

TEST_F(HelpTest, XffOperatorIsTaggedXff) {
  EXPECT_THAT(RenderEntry("-xor"), AllOf(HasSubstr("operator"), HasSubstr("xff")));
}

TEST_F(HelpTest, SecurityActionIsTagged) {
  EXPECT_THAT(RenderEntry("-exec"), HasSubstr("runs commands"));
}

TEST_F(HelpTest, VariadicArgHintShowsCommandForm) {
  EXPECT_THAT(RenderEntry("-exec"), HasSubstr("CMD..."));
}

TEST_F(HelpTest, UnknownTopicResolvesToNothingInTheModel) {
  // An unknown topic matches no model reference (entry / topic / index), so the CLI
  // reports unknown-topic (verified end to end in help_topic_test.sh).
  EXPECT_THAT(EntryReference("-bogus"), Eq(std::nullopt));
  EXPECT_THAT(TopicReference("-bogus"), Eq(std::nullopt));
  EXPECT_THAT(IndexReference("-bogus"), Eq(std::nullopt));
}

TEST_F(HelpTest, ListIndexRendersEveryPrimaryAndTheTopicMap) {
  // `--help=list` is the whole-vocabulary index (the usage page): every expression
  // primary grouped by kind, plus the `--help=TOPIC` map.
  const std::string out = RenderIndex("list");
  EXPECT_THAT(out, AllOf(HasSubstr("Tests"), HasSubstr("Actions"), HasSubstr("Operators")));
  for (const registry::Descriptor& descriptor : registry::All()) {
    EXPECT_THAT(out, HasSubstr(descriptor.name)) << descriptor.name;
  }
  EXPECT_THAT(out, HasSubstr("--help=TOPIC"));  // the help / topic map
}

TEST_F(HelpTest, FullReferenceHasDetailsAllIndexIsSummariesOnly) {
  // The full reference (BuildReference, = --help=full) carries the long per-entry
  // explanations; `--help=all` is the same set summaries-only -- strictly shorter.
  const std::string full = RenderDoc(BuildReference());
  const std::string all = RenderIndex("all");
  EXPECT_THAT(full, AllOf(HasSubstr("--sort"), HasSubstr("-regex"), HasSubstr("A config style sets")));
  EXPECT_THAT(all, AllOf(HasSubstr("--sort"), HasSubstr("-regex")));
  EXPECT_THAT(all, Not(HasSubstr("A config style sets")));  // summaries only, no detail prose
  EXPECT_THAT(all, SizeIs(Lt(full.size())));                // strictly shorter than the full reference
}

TEST_F(HelpTest, SingleFlagHelpShowsTheLongExplanation) {
  // `--help=NAME` for a flag with a `details` paragraph shows it (not just the summary).
  EXPECT_THAT(RenderEntry("--config"), HasSubstr("A config style sets"));
  EXPECT_THAT(RenderEntry("--time-format"), HasSubstr("per-field qualifier"));
}

TEST_F(HelpTest, ValuedFlagDocumentsItsPlaceholderValues) {
  // A flag whose synopsis collapses a value grammar to a `<PLACEHOLDER>` documents each
  // allowed value + meaning as a table in its detail tier, so the literal tokens are not
  // lost from the shortened synopsis.
  const std::string summary = RenderEntry("--summary");
  EXPECT_THAT(
      summary, AllOf(
                   HasSubstr("--summary[=<GROUP>]"), HasSubstr("GROUP is one of:"), HasSubstr("ext"),
                   HasSubstr("by extension"), HasSubstr("lang"), HasSubstr("{template}")));
  const std::string regextype = RenderEntry("--regextype");
  EXPECT_THAT(
      regextype, AllOf(
                     HasSubstr("--regextype=<GRAMMAR>"), HasSubstr("GRAMMAR is one of:"), HasSubstr("RE2"),
                     HasSubstr("linear-time"), HasSubstr("PCRE2")));
}

TEST_F(HelpTest, ColorContextEmitsThePalette) {
  // The full reference exercises every colored node kind. With color on, the plain
  // backend emits the palette: bold headings, bold-cyan entry terms (flag / primary
  // names), cyan value-table terms, and green verbatim example code. (auto vs always is
  // resolved at the CLI boundary; the backend only sees the resolved bool.)
  const Document doc = BuildReference();
  PlainTextBackend colored(HelpRenderContext{.color = true});
  RenderDocument(doc, colored);
  const std::string out = colored.Take();
  EXPECT_THAT(out, HasSubstr("\x1b[1m"));     // headings: bold
  EXPECT_THAT(out, HasSubstr("\x1b[1;36m"));  // names: bold cyan
  EXPECT_THAT(out, HasSubstr("\x1b[36m"));    // values / items: cyan
  EXPECT_THAT(out, HasSubstr("\x1b[32m"));    // example code: green
}

TEST_F(HelpTest, ColorContextColorsThePreamble) {
  // The identity line colors the program name (bold cyan); the Usage line colors the
  // whole `name synopsis` command as code (green). BuildUsage carries a preamble.
  PlainTextBackend colored(HelpRenderContext{.color = true});
  RenderDocument(BuildUsage(), colored);
  const std::string out = colored.Take();
  EXPECT_THAT(out, HasSubstr("\x1b[1;36mxff\x1b[0m - "));  // name bold cyan, tagline plain
  EXPECT_THAT(out, HasSubstr("Usage: \x1b[32mxff "));      // usage command green
}

TEST_F(HelpTest, PlainContextEmitsNoAnsi) {
  // The default (color off) renders byte-for-byte the same as before: zero escapes.
  const Document doc = BuildReference();
  PlainTextBackend plain(HelpRenderContext{.color = false});
  RenderDocument(doc, plain);
  EXPECT_THAT(plain.Take(), Not(HasSubstr("\x1b[")));
}

TEST_F(HelpTest, UsagePageHelpSectionListsFlagsAndTopics) {
  // The usage page's Help section is built from HelpFlags() + the topic index (the model's
  // BuildHelpSection), not a hand-written string: the meta/doc flags plus the nested topics.
  EXPECT_THAT(HelpFlags(), Not(IsEmpty()));
  const std::string usage = RenderDoc(BuildUsage());
  EXPECT_THAT(usage, AllOf(HasSubstr("--help=TOPIC"), HasSubstr("--man"), HasSubstr("--version")));
  EXPECT_THAT(usage, HasSubstr("fields"));  // the topic index (HelpTopics) is nested in
}

TEST_F(HelpTest, HelpGuideListsEveryTopic) {
  // `--help=help` is the help-system guide, rendered from the model (TopicReference reusing
  // BuildHelpSection); it lists every topic from the SOT.
  const std::string guide = RenderTopicDoc("help");
  EXPECT_THAT(guide, HasSubstr("Topics"));
  for (const HelpTopic& topic : HelpTopics()) {
    EXPECT_THAT(guide, HasSubstr(topic.name)) << topic.name;
  }
}

TEST_F(HelpTest, HelpGuideListsTheDisplayFlags) {
  // The guide surfaces the output globals that shape how help itself renders, and explains
  // that --pager pages --help / --man on a terminal.
  const std::string guide = RenderTopicDoc("help");
  EXPECT_THAT(
      guide, AllOf(
                 HasSubstr("Display options"), HasSubstr("--color"), HasSubstr("--pager"), HasSubstr("--width"),
                 HasSubstr("paged")));
}

TEST_F(HelpTest, TimeTopicExplainsZoneControlAndSuffix) {
  // `--help=time` documents --timezone and the --time-zone-suffix control (drop / force
  // the zone suffix), including that a custom pattern is left alone.
  const std::string time = RenderTopicDoc("time");
  EXPECT_THAT(time, AllOf(HasSubstr("--timezone"), HasSubstr("--time-zone-suffix"), HasSubstr("custom strftime")));
}

TEST_F(HelpTest, EnvironmentTopicListsTheVariables) {
  // `--help=environment` (env) documents the variables xff reads, and the alias resolves.
  const std::string env = RenderTopicDoc("environment");
  EXPECT_THAT(
      env, AllOf(
               HasSubstr("NO_COLOR"), HasSubstr("XFF_PAGER"), HasSubstr("XFF_MANPAGER"), HasSubstr("COLUMNS"),
               HasSubstr("XFF_CONFIG"), HasSubstr("{env.NAME}")));
  EXPECT_THAT(RenderTopicDoc("env"), Eq(env));  // the alias renders identically
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): a flat per-topic drift sweep.
TEST_F(HelpTest, EveryAdvertisedTopicResolvesInTheModel) {
  // Drift guard: every advertised topic resolves to a non-empty model document via
  // TopicReference or IndexReference, and each alias renders identically. styles /
  // extras are CLI-runtime topics (they need facets above this library), so excluded.
  const auto render = [](std::string_view name) -> std::string {
    if (name == "full" || name == "long") {
      return RenderDoc(BuildReference());  // the full reference (FullReference in the CLI)
    }
    if (const std::optional<Document> doc = TopicReference(name); doc.has_value()) {
      return RenderDoc(*doc);
    }
    if (const std::optional<Document> doc = IndexReference(name); doc.has_value()) {
      return RenderDoc(*doc);
    }
    return "";
  };
  for (const HelpTopic& topic : HelpTopics()) {
    if (topic.name == "styles" || topic.name == "extras") {
      continue;  // CLI-runtime topics, rendered above this library
    }
    const std::string out = render(topic.name);
    EXPECT_THAT(out, Not(IsEmpty())) << topic.name;
    for (const std::string_view alias : topic.aliases) {
      EXPECT_THAT(render(alias), Eq(out)) << alias;  // an alias is a pure synonym
    }
  }
}

TEST_F(HelpTest, ExpressionsIndexListsEveryPrimaryWithoutGlobals) {
  // `--help=expressions` is the annotated Tests/Actions/Operators list -- every
  // expression primary with its summary, but not the whole-run global flags.
  const std::string expr = RenderIndex("expressions");
  EXPECT_THAT(expr, AllOf(HasSubstr("Tests"), HasSubstr("Actions"), HasSubstr("Operators")));
  for (const registry::Descriptor& descriptor : registry::All()) {
    EXPECT_THAT(expr, HasSubstr(descriptor.name)) << descriptor.name;
  }
  EXPECT_THAT(expr, Not(HasSubstr("Traversal")));  // no whole-run global-flag groups
}

TEST_F(HelpTest, StatsTopicDocumentsSummaryAndHistogram) {
  // `--help=stats` renders from the model (TopicReference) and covers both reductions:
  // --summary, --histogram, the numeric-metric grammar, and the no-bare-metric rule.
  EXPECT_THAT(
      RenderTopicDoc("stats"),
      AllOf(
          HasSubstr("--summary"), HasSubstr("--histogram"), HasSubstr("sum(lines)"), HasSubstr("needs an aggregator")));
}

TEST_F(HelpTest, ConfigTopicDocumentsTiersStyleAndArming) {
  // `--help=config` renders from the model (TopicReference): the layered tiers, style
  // selection (--config / argv[0]), and the arming rule for dangerous --xffrc directives.
  EXPECT_THAT(
      RenderTopicDoc("config"), AllOf(
                                    HasSubstr("system config"), HasSubstr("command line"), HasSubstr("--config"),
                                    HasSubstr("argv[0]"), HasSubstr("--allow-exec")));
}

TEST_F(HelpTest, NoticeTopicReproducesTheManifestVerbatim) {
  // `--help=notice` renders from the model: the build-dependent extras line, then the
  // verbatim third-party NOTICE manifest (reproduced, not a file pointer).
  const std::string notice = RenderTopicDoc("notice");
  EXPECT_THAT(notice, AllOf(HasSubstr("Build extras compiled into this binary"), HasSubstr("RE2")));
}

TEST_F(HelpTest, LicenseTopicLeadsWithCopyrightThenTheText) {
  // `--help=license` renders from the model (a title-less verbatim block): the copyright
  // + grant lead (task #142), then the full Apache-2.0 text.
  EXPECT_THAT(
      RenderTopicDoc("license"),
      AllOf(HasSubstr("eXtended File Find"), HasSubstr("Apache License"), HasSubstr("Version 2.0")));
}

TEST_F(HelpTest, GlobalFlagTopicRendersWithGlobalTag) {
  EXPECT_THAT(RenderEntry("--sort"), AllOf(HasSubstr("--sort"), HasSubstr("ordering"), HasSubstr("global")));
}

TEST_F(HelpTest, GlobalFlagResolvesByAliasAndDashless) {
  EXPECT_THAT(RenderEntry("-j"), HasSubstr("--jobs"));    // alias -> --jobs
  EXPECT_THAT(RenderEntry("sort"), HasSubstr("--sort"));  // dash-less -> --sort
}

TEST_F(HelpTest, ListIndexIncludesGlobalGroupsAndEveryFlag) {
  // `--help=list` (the usage-page index) groups the whole-run flags by header and lists
  // every one, including a not-built extra flag (which carries its rebuild note).
  const std::string index = RenderIndex("list");
  EXPECT_THAT(index, AllOf(HasSubstr("Config"), HasSubstr("Traversal")));
  for (const GlobalFlag& flag : Globals()) {
    EXPECT_THAT(index, HasSubstr(flag.name)) << flag.name;
  }
}

TEST_F(HelpTest, DetailedHelpShowsInfluenceCrossReferences) {
  // A primary's detailed help lists the globals that change it (the reverse of their affects
  // declarations), under an indented "Affected by:" sub-header.
  EXPECT_THAT(
      RenderEntry("-diff"),
      AllOf(
          HasSubstr("Affected by:"), HasSubstr("--diff-format"), HasSubstr("--diff-context"), HasSubstr("--context")));
  EXPECT_THAT(
      RenderEntry("-grep"), AllOf(HasSubstr("Affected by:"), HasSubstr("--count"), HasSubstr("--after-context")));
  // A flag <-> flag edge points from the feeder to the fed: --context supplies the -diff context
  // when --diff-context is absent, so --context lists --diff-context under "Affects:" and
  // --diff-context shows the reverse "Affected by:" (never the other way around).
  EXPECT_THAT(
      RenderEntry("--context"),
      AllOf(HasSubstr("Affects:"), HasSubstr("-grep"), HasSubstr("--diff-context"), Not(HasSubstr("Affected by:"))));
  EXPECT_THAT(
      RenderEntry("--diff-context"),
      AllOf(HasSubstr("Affects:"), HasSubstr("-diff"), HasSubstr("Affected by:"), HasSubstr("--context")));
  // A global that nothing supersedes shows Affects: but no Affected by:.
  EXPECT_THAT(RenderEntry("--diff-format"), AllOf(HasSubstr("Affects:"), Not(HasSubstr("Affected by:"))));
}

TEST_F(HelpTest, InfluenceBlocksAreTheDetailTierOnly) {
  // The full reference (detailed tier) carries the influence blocks; --help=all (summaries) omits them.
  EXPECT_THAT(RenderDoc(BuildReference()), HasSubstr("Affected by:"));
  EXPECT_THAT(RenderIndex("all"), Not(HasSubstr("Affected by:")));
}

TEST_F(HelpTest, EveryAffectsTokenResolvesToARealEntry) {
  // Drift guard: each token a global declares in `affects` must resolve to a real expression
  // primary or global flag, so the "Affects:" / "Affected by:" cross-references never dangle after
  // a rename. (A leading-'@' category token is a planned extension and is intentionally not used
  // yet -- it would fail this check until the resolver learns to expand it.)
  for (const GlobalFlag& flag : Globals()) {
    for (const std::string_view token : absl::StrSplit(flag.affects, ',', absl::SkipEmpty())) {
      const bool resolves = registry::Lookup(token) != nullptr || LookupGlobal(token) != nullptr;
      EXPECT_TRUE(resolves) << flag.name << " affects unknown entry '" << token << "'";
    }
  }
}

}  // namespace
}  // namespace xff::cli
