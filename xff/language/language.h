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

#ifndef XFF_LANGUAGE_LANGUAGE_H_
#define XFF_LANGUAGE_LANGUAGE_H_

#include <string_view>

namespace xff::language {

// The programming/markup language for the file named `name` (a basename such as "main.cc",
// "Makefile", or ".bashrc"), as a canonical github-linguist name ("C++", "Python", "Shell",
// ...). An exact filename match wins (Makefile, Dockerfile, CMakeLists.txt, BUILD, ...); else
// the extension is looked up case-insensitively (`.PY` == `.py`). Returns "" when the name has
// no recognized filename or extension.
//
// Extension/filename-based, not content-classified: a curated static table of common languages
// (no github-linguist dependency, no YAML parsing -- a deliberate no-heavyweight-dep choice, the
// same call the mime module makes). The heuristics linguist layers on top (shebang / modeline /
// content disambiguation of `.h`, `.m`, `.pl`, ...) are out of scope; this is a fast,
// dependency-free first cut backing the `-lang GLOB` predicate and the `{lang}` field.
std::string_view LanguageForName(std::string_view name);

}  // namespace xff::language

#endif  // XFF_LANGUAGE_LANGUAGE_H_
