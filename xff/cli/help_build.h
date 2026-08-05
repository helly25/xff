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

#ifndef XFF_CLI_HELP_BUILD_H_
#define XFF_CLI_HELP_BUILD_H_

#include "xff/cli/help_model.h"

// Builds the complete help document from the single source of truth (#154 slice D):
// the global flags, the expression registry, and the documentation-source
// vocabularies (fields / printf / time / size / regex grammars). This is the model
// counterpart of the imperative WriteReference() walk - it produces a help_model
// Document that any HelpBackend renders (see help_backend.h), so a new output format
// is a new backend and every format shares this one build. Authored doc strings are
// parsed for their backtick / fence markup by help_parse; structure (sections,
// entries, rows) is built as typed nodes here.
namespace xff::cli {

// Assembles the reference Document from the SOT. Called once per help render.
[[nodiscard]] Document BuildReference();

}  // namespace xff::cli

#endif  // XFF_CLI_HELP_BUILD_H_
