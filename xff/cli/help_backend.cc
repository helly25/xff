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

#include "xff/cli/help_backend.h"

#include <variant>

#include "xff/cli/help_model.h"

namespace xff::cli {
namespace {

// Walks one block list in order. Factored out so it recurses into the nested
// Blocks that Subsection and Entry carry (a section's children, an entry's details).
void RenderBlocks(const Blocks& blocks, HelpBackend& backend) {
  for (const Content& content : blocks) {
    std::visit(
        [&backend](const auto& node) {
          using Node = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<Node, Prose>) {
            backend.EmitProse(node);
          } else if constexpr (std::is_same_v<Node, Example>) {
            backend.EmitExample(node);
          } else if constexpr (std::is_same_v<Node, Bullets>) {
            backend.EmitBullets(node);
          } else if constexpr (std::is_same_v<Node, Rows>) {
            backend.EmitRows(node);
          } else if constexpr (std::is_same_v<Node, Table>) {
            backend.EmitTable(node);
          } else if constexpr (std::is_same_v<Node, SeeAlso>) {
            backend.EmitSeeAlso(node);
          } else if constexpr (std::is_same_v<Node, Entry>) {
            backend.BeginEntry(node);
            RenderBlocks(node.details, backend);
            backend.EndEntry(node);
          } else if constexpr (std::is_same_v<Node, Subsection>) {
            backend.BeginSubsection(node);
            RenderBlocks(node.children, backend);
            backend.EndSubsection(node);
          }
        },
        content.node);
  }
}

}  // namespace

void RenderDocument(const Document& doc, HelpBackend& backend) {
  backend.Preamble(doc);
  for (const Section& section : doc.sections) {
    backend.BeginSection(section);
    RenderBlocks(section.children, backend);
    backend.EndSection(section);
  }
}

}  // namespace xff::cli
