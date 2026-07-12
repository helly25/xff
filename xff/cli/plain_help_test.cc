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

#include "xff/cli/plain_help.h"

#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "xff/cli/doc_renderer.h"

namespace xff::cli {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::WithDropIndent;
using ::testing::AllOf;
using ::testing::HasSubstr;

struct PlainHelpTest : ::testing::Test {};

TEST_F(PlainHelpTest, RendersEveryPrimitiveInTheHouseStyle) {
  // Drives each DocRenderer primitive once: uppercase Section, `Title:` Subsection, backtick-dropped
  // Prose, aligned Rows (widest term + 2), 2-space bulleted lists, an xff-tagged Entry, a verbatim
  // Example, and the SEE ALSO refs + note. Blocks are separated by exactly one blank line.
  PlainRenderer renderer;
  renderer.Document("xff", "eXtended File Find", "[opt] [path]");
  renderer.Section("Fields");
  renderer.Prose("The `{field}` vocabulary and the `%{esc}` escape.");
  renderer.Subsection("Group one");
  const std::vector<DocRow> rows = {{"{a}", "alpha"}, {"{bb}", "beta"}};
  renderer.Rows(rows);
  renderer.Subsection("Braces");
  const std::vector<std::string_view> bullets = {"`{{` emits a brace", "unknown renders empty"};
  renderer.Bullets(bullets);
  renderer.Entry("--flag", "does a thing", "the long detail", /*xff=*/true);
  renderer.Example("  x | y\n  a   b");
  const std::vector<CrossRef> refs = {{.name = "find", .section = "1"}, {.name = "grep", .section = "1"}};
  renderer.SeeAlso(refs, "see also `stuff`.");

  EXPECT_THAT(renderer.Take(), WithDropIndent(EqualsText(R"out(
      xff - eXtended File Find

      Usage: xff [opt] [path]

      FIELDS

      The {field} vocabulary and the %{esc} escape.

      Group one:
        {a}   alpha
        {bb}  beta

      Braces:
        - {{ emits a brace
        - unknown renders empty

      --flag  (xff)
          does a thing
          the long detail

        x | y
        a   b

      find(1), grep(1)

      see also stuff.
      )out")));
}

TEST_F(PlainHelpTest, WriteFieldsThroughPlainRendererCarriesTheVocabulary) {
  // The shared WriteFields() walk, driven through the plain renderer (the retired RenderFields
  // path): the topic keeps its group headings, folded aliases, dynamic namespaces, qualifiers,
  // and the m// span diagram - now from the one source that also feeds --man / --markdown.
  PlainRenderer renderer;
  WriteFields(renderer);
  EXPECT_THAT(
      renderer.Take(), AllOf(
                           HasSubstr("FIELDS"), HasSubstr("Path & name:"), HasSubstr("{relpath}"),
                           HasSubstr("{name} {file}"), HasSubstr("{env.NAME}"), HasSubstr("{name:s/RE/R/f}"),
                           HasSubstr("stem"), HasSubstr("|________| |________| |_______| |________|"),
                           HasSubstr("extract    map each   reduce    rewrite")));
}

}  // namespace
}  // namespace xff::cli
