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
using ::testing::Ne;
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
  const std::optional<Document> list = IndexReference("list");
  ASSERT_THAT(list, Ne(std::nullopt));
  const std::string out = RenderDoc(*list);
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
  const std::optional<Document> all_doc = IndexReference("all");
  ASSERT_THAT(all_doc, Ne(std::nullopt));
  const std::string all = RenderDoc(*all_doc);
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
  const std::optional<Document> expr_doc = IndexReference("expressions");
  ASSERT_THAT(expr_doc, Ne(std::nullopt));
  const std::string expr = RenderDoc(*expr_doc);
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
  const std::optional<Document> list = IndexReference("list");
  ASSERT_THAT(list, Ne(std::nullopt));
  const std::string index = RenderDoc(*list);
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
  const std::optional<Document> all_doc = IndexReference("all");
  ASSERT_THAT(all_doc, Ne(std::nullopt));
  EXPECT_THAT(RenderDoc(BuildReference()), HasSubstr("Affected by:"));
  EXPECT_THAT(RenderDoc(*all_doc), Not(HasSubstr("Affected by:")));
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
