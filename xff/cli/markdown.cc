// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
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

#include "xff/cli/markdown.h"

#include <string>

#include "xff/cli/help_backend.h"
#include "xff/cli/help_build.h"
#include "xff/cli/markdown_backend.h"

namespace xff::cli {

std::string MarkdownReference() {
  MarkdownBackend backend;
  // Audience::kPublished: XFF.md documents the whole tool, so a flag whose build extra is absent from
  // the binary that generated this file is still documented in full. Its details still say it is a
  // build extra and name the flag to rebuild with, and `--help=extras` reports what a given binary
  // actually links - so a reader is told what they may not have, without the reference pretending the
  // feature does not exist.
  RenderDocument(BuildReference(Audience::kPublished), backend);
  return backend.Take();
}

}  // namespace xff::cli
