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

#include "xff/license/license.h"

#include <string>

#include "absl/strings/str_cat.h"

namespace xff::license {
namespace {

// The always-linked core dependencies. Kept in this TU (the one that defines NoticeText) so the
// linker never drops them: NoticeText is referenced, so this TU -- and its registrars -- are pulled
// in. A build-extra registers from its own TU instead, so it appears exactly when it is linked.
const Registrar kAbseil{
    {.component = "Abseil (C++)",
     .spdx = "Apache-2.0",
     .text = "Copyright The Abseil Authors. Licensed under the Apache License, Version 2.0."}};
const Registrar kRe2{
    {.component = "RE2",
     .spdx = "BSD-3-Clause",
     .text = "Copyright (c) 2009 The RE2 Authors. Redistribution permitted under the BSD-3-Clause license."}};
const Registrar kMbo{
    {.component = "helly25/mbo",
     .spdx = "Apache-2.0",
     .text = "Copyright helly25. Licensed under the Apache License, Version 2.0."}};

}  // namespace

std::string NoticeText() {
  std::string out =
      "xff - eXtended File Find\n"
      "Copyright 2026 Marcus Boerger / helly25\n"
      "Licensed under the Apache License, Version 2.0 (see the LICENSE file / --help=license).\n"
      "\n"
      "This product links the third-party components below; all are under permissive licenses\n"
      "(no copyleft). The notice-retention obligation is met by reproducing each name, SPDX license\n"
      "identifier, and copyright line.\n";
  for (const Notice& notice : Notices()) {
    absl::StrAppend(&out, "\n", notice.component, "  [", notice.spdx, "]\n  ", notice.text, "\n");
  }
  return out;
}

}  // namespace xff::license
