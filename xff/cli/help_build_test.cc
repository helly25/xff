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

#include <string>
#include <variant>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/cli/help_model.h"

namespace xff::cli {
namespace {

using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::SizeIs;

std::vector<std::string> SectionTitles(const Document& doc) {
  std::vector<std::string> titles;
  titles.reserve(doc.sections.size());
  for (const Section& section : doc.sections) {
    titles.push_back(section.title);
  }
  return titles;
}

// The subsection titles directly under a section (skips non-subsection children).
std::vector<std::string> SubsectionTitles(const Section& section) {
  std::vector<std::string> titles;
  for (const Content& child : section.children) {
    if (const auto* sub = std::get_if<Subsection>(&child.node)) {
      titles.push_back(sub->title);
    }
  }
  return titles;
}

const Section& SectionNamed(const Document& doc, std::string_view title) {
  for (const Section& section : doc.sections) {
    if (section.title == title) {
      return section;
    }
  }
  ADD_FAILURE() << "no section titled " << title;
  static const Section kEmpty;
  return kEmpty;
}

struct BuildReferenceTest : ::testing::Test {
  Document doc = BuildReference();
};

TEST_F(BuildReferenceTest, PreambleComesFromTheSot) {
  EXPECT_THAT(doc.name, Eq("xff"));
  EXPECT_THAT(doc.tagline, Not(IsEmpty()));
  EXPECT_THAT(doc.usage, Eq("[option...] [path...] [expression]"));
}

TEST_F(BuildReferenceTest, SectionsAppearInReferenceOrder) {
  EXPECT_THAT(
      SectionTitles(doc), ElementsAre(
                              "Description", "Options", "Expression", "Fields", "Printf directives", "Time formats",
                              "Size units", "Regex grammars", "Statistics", "Examples", "Exit status", "See also"));
}

TEST_F(BuildReferenceTest, ExpressionHasTheThreeKindSubsections) {
  EXPECT_THAT(SubsectionTitles(SectionNamed(doc, "Expression")), ElementsAre("Tests", "Actions", "Operators"));
}

TEST_F(BuildReferenceTest, OptionsGroupsFlagsIntoNonEmptySubsections) {
  const Section& options = SectionNamed(doc, "Options");
  ASSERT_THAT(options.children, Not(IsEmpty()));
  const auto* first = std::get_if<Subsection>(&options.children.front().node);
  ASSERT_NE(first, nullptr);
  // Each grouped flag is an Entry nested under its subsection.
  bool has_entry = false;
  for (const Content& child : first->children) {
    has_entry = has_entry || std::holds_alternative<Entry>(child.node);
  }
  EXPECT_TRUE(has_entry);
}

TEST_F(BuildReferenceTest, FieldsCarriesTheBracesAndQualifiersSubsections) {
  const std::vector<std::string> subs = SubsectionTitles(SectionNamed(doc, "Fields"));
  EXPECT_THAT(subs, Contains("Braces"));
  EXPECT_THAT(subs, Contains("Dynamic namespaces"));
  EXPECT_THAT(subs, Contains("Qualifiers ({field:QUAL})"));
}

TEST_F(BuildReferenceTest, SeeAlsoCarriesManPageCrossReferences) {
  const Section& see_also = SectionNamed(doc, "See also");
  ASSERT_THAT(see_also.children, SizeIs(1));
  const auto* block = std::get_if<SeeAlso>(&see_also.children.front().node);
  ASSERT_NE(block, nullptr);
  EXPECT_THAT(block->refs, Not(IsEmpty()));
  EXPECT_THAT(block->refs.front().id, Eq("find"));
}

}  // namespace
}  // namespace xff::cli
