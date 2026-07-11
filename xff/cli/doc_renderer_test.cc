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

#include <string>
#include <string_view>
#include <vector>

#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/cli/globals.h"
#include "xff/cli/help.h"
#include "xff/registry/descriptor.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::StartsWith;
using ::testing::UnorderedElementsAre;

// One recorded DocRenderer call, so the shared walk / Markdown parser can be asserted
// structurally (independent of any one output format's escaping).
struct Event {
  std::string kind;
  std::string text;
};

// A DocRenderer that records the structural calls it receives instead of formatting them.
class Recorder final : public DocRenderer {
 public:
  void Document(std::string_view name, std::string_view /*tagline*/, std::string_view /*usage*/) override {
    events_.push_back({.kind = "document", .text = std::string(name)});
  }

  void Section(std::string_view title) override { events_.push_back({.kind = "section", .text = std::string(title)}); }

  void Subsection(std::string_view title) override {
    events_.push_back({.kind = "subsection", .text = std::string(title)});
  }

  void Prose(std::string_view text) override { events_.push_back({.kind = "prose", .text = std::string(text)}); }

  void Bullets(absl::Span<const std::string_view> items) override {
    for (const std::string_view item : items) {
      events_.push_back({.kind = "bullet", .text = std::string(item)});
    }
  }

  void Entry(std::string_view term, std::string_view /*summary*/, std::string_view /*details*/, bool /*xff*/) override {
    events_.push_back({.kind = "entry", .text = std::string(term)});
  }

  void Rows(absl::Span<const DocRow> rows) override {
    for (const DocRow& row : rows) {
      events_.push_back({.kind = "row", .text = std::string(row.first)});
    }
  }

  void Example(std::string_view /*text*/) override { events_.push_back({.kind = "example", .text = ""}); }

  void SeeAlso(absl::Span<const CrossRef> /*refs*/, std::string_view /*note*/) override {
    events_.push_back({.kind = "seealso", .text = ""});
  }

  std::string Take() override { return ""; }

  [[nodiscard]] const std::vector<Event>& events() const { return events_; }

  // The recorded text of every call of a given kind, in order.
  [[nodiscard]] std::vector<std::string> Texts(std::string_view kind) const {
    std::vector<std::string> out;
    for (const Event& event : events_) {
      if (event.kind == kind) {
        out.push_back(event.text);
      }
    }
    return out;
  }

 private:
  std::vector<Event> events_;
};

testing::Matcher<Event> EventIs(std::string_view kind, std::string_view text) {
  return AllOf(Field("kind", &Event::kind, kind), Field("text", &Event::text, text));
}

struct DocRendererTest : ::testing::Test {};

TEST_F(DocRendererTest, WriteMarkdownParsesHeadingsBulletsAndParagraphs) {
  Recorder rec;
  WriteMarkdown(rec, "# Title\n\nfirst line\nsecond line\n\n- one\n- two\n\n## Sub\ntail");
  EXPECT_THAT(
      rec.events(),
      ElementsAre(
          EventIs("section", "Title"), EventIs("prose", "first line second line"), EventIs("bullet", "one"),
          EventIs("bullet", "two"), EventIs("subsection", "Sub"), EventIs("prose", "tail")));
}

TEST_F(DocRendererTest, WriteMarkdownTreatsALoneHashAsText) {
  Recorder rec;
  WriteMarkdown(rec, "#nospace is not a heading");
  EXPECT_THAT(rec.events(), ElementsAre(EventIs("prose", "#nospace is not a heading")));
}

TEST_F(DocRendererTest, ReferenceEmitsTheExpectedTopLevelSections) {
  Recorder rec;
  WriteReference(rec);
  EXPECT_THAT(
      rec.Texts("section"), ElementsAre(
                                "Description", "Options", "Expression", "Fields", "Printf directives", "Time formats",
                                "Size units", "Regex grammars", "Examples", "Exit status", "See also"));
}

// Drift guard: the walk hard-codes the sub-vocabulary sections (fields / printf / time / size /
// cookbook). If a new in_full topic is added to HelpTopics(), this fails, prompting the walk (and
// the man / Markdown / full-help reference it feeds) to gain the matching section.
TEST_F(DocRendererTest, InFullTopicsMatchTheReferenceWalk) {
  std::vector<std::string> in_full;
  for (const HelpTopic& topic : HelpTopics()) {
    if (topic.in_full) {
      in_full.emplace_back(topic.name);
    }
  }
  EXPECT_THAT(in_full, UnorderedElementsAre("fields", "printf", "time", "size", "grammars", "cookbook"));
}

// Completeness guards: the shared walk feeds --man and --markdown, so every expression primary
// and every global flag must reach it. This is the exact class of drift that motivated the walk
// (man/markdown had silently lagged the vocabulary), now locked at the source.
TEST_F(DocRendererTest, ReferenceEntriesCoverEveryPrimary) {
  Recorder rec;
  WriteReference(rec);
  const std::vector<std::string> entries = rec.Texts("entry");
  for (const registry::Descriptor& descriptor : registry::All()) {
    // Entry terms are "<name><arg-hint>", so the primary name is a prefix of its entry.
    EXPECT_THAT(entries, Contains(StartsWith(descriptor.name))) << descriptor.name;
  }
}

TEST_F(DocRendererTest, ReferenceEntriesCoverEveryGlobalFlag) {
  Recorder rec;
  WriteReference(rec);
  const std::vector<std::string> entries = rec.Texts("entry");
  for (const GlobalFlag& flag : Globals()) {
    EXPECT_THAT(entries, Contains(std::string(flag.display))) << flag.name;
  }
}

}  // namespace
}  // namespace xff::cli
