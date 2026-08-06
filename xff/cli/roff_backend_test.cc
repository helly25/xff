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

#include "xff/cli/roff_backend.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/cli/help_backend.h"
#include "xff/cli/help_model.h"

namespace xff::cli {
namespace {

using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::StartsWith;

Inline Text(std::string text) {
  return {.style = Inline::Style::kText, .text = std::move(text)};
}

Inline Code(std::string text) {
  return {.style = Inline::Style::kCode, .text = std::move(text)};
}

struct RoffEscapeTest : ::testing::Test {};

TEST_F(RoffEscapeTest, EscapesHyphensBackslashesAndLeadingControls) {
  EXPECT_THAT(RoffEscape("--summary"), Eq(R"(\-\-summary)"));
  EXPECT_THAT(RoffEscape(R"(a\b)"), Eq(R"(a\\b)"));
  EXPECT_THAT(RoffEscape(".hidden"), Eq(R"(\&.hidden)"));   // leading dot guarded
  EXPECT_THAT(RoffEscape("plain text"), Eq("plain text"));  // nothing to escape
}

struct RoffBackendTest : ::testing::Test {};

TEST_F(RoffBackendTest, RendersManPageStructure) {
  const Document doc{
      .name = "xff",
      .tagline = "eXtended File Find",
      .usage = "xff [path]",
      .sections =
          {
              Section{
                  .title = "Options",  // natural case in the model ...
                  .children =
                      {
                          Content{
                              .node =
                                  Entry{
                                      .term = "--summary",
                                      .summary = {Text("group + "), Code("aggregate")},
                                      .xff = true}},
                      },
              },
              Section{
                  .title = "See also",
                  .children =
                      {
                          Content{
                              .node =
                                  SeeAlso{.refs = {{.kind = RefTarget::Kind::kManPage, .id = "find", .section = "1"}}}},
                      },
              },
          },
  };

  RoffBackend backend;
  RenderDocument(doc, backend);
  const std::string out = backend.Take();
  // roff control directives are only valid in the first column, so each check anchors
  // to a line start: `.TH` opens the file, the rest are preceded by a newline.
  EXPECT_THAT(out, StartsWith(".TH xff 1"));
  EXPECT_THAT(out, HasSubstr("\n.SH NAME\nxff \\- eXtended File Find\n"));
  EXPECT_THAT(out, HasSubstr("\n.SH OPTIONS\n"));  // ... upper-cased for roff
  EXPECT_THAT(out, HasSubstr("\n.TP\n.B \\-\\-summary\n"));
  EXPECT_THAT(out, HasSubstr("group + \\fBaggregate\\fR (xff extension)"));
  EXPECT_THAT(out, HasSubstr("\n.BR find (1)\n"));
}

}  // namespace
}  // namespace xff::cli
