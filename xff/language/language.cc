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

#include "xff/language/language.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/ascii.h"
#include "mbo/container/limited_map.h"

namespace xff::language {
namespace {

namespace stdfs = std::filesystem;

using Entry = std::pair<std::string_view, std::string_view>;

// Exact basename -> language, for languages identified by filename rather than extension
// (sorted by key for the constexpr LimitedMap). Case-sensitive: `Makefile` and `makefile` are
// listed separately because that is how they occur.
constexpr auto kFilenameLanguages = mbo::container::MakeLimitedMap(
    Entry{".bash_profile", "Shell"},
    Entry{".bashrc", "Shell"},
    Entry{".profile", "Shell"},
    Entry{".zshrc", "Shell"},
    Entry{"BUILD", "Starlark"},
    Entry{"BUILD.bazel", "Starlark"},
    Entry{"CMakeLists.txt", "CMake"},
    Entry{"Dockerfile", "Dockerfile"},
    Entry{"GNUmakefile", "Makefile"},
    Entry{"Gemfile", "Ruby"},
    Entry{"MODULE.bazel", "Starlark"},
    Entry{"Makefile", "Makefile"},
    Entry{"Rakefile", "Ruby"},
    Entry{"WORKSPACE", "Starlark"},
    Entry{"WORKSPACE.bazel", "Starlark"},
    Entry{"makefile", "Makefile"});

// Lowercase extension (no dot) -> language, sorted by key for the constexpr LimitedMap. Curated
// to common languages; ambiguous extensions take github-linguist's default (`.h` = C, `.m` =
// Objective-C). Unknown extensions return "" in LanguageForName, so they need no entry.
constexpr auto kExtensionLanguages = mbo::container::MakeLimitedMap(
    Entry{"asm", "Assembly"},
    Entry{"awk", "Awk"},
    Entry{"bash", "Shell"},
    Entry{"bat", "Batchfile"},
    Entry{"bzl", "Starlark"},
    Entry{"c", "C"},
    Entry{"cc", "C++"},
    Entry{"clj", "Clojure"},
    Entry{"cljc", "Clojure"},
    Entry{"cljs", "Clojure"},
    Entry{"cmake", "CMake"},
    Entry{"cpp", "C++"},
    Entry{"cs", "C#"},
    Entry{"css", "CSS"},
    Entry{"cxx", "C++"},
    Entry{"d", "D"},
    Entry{"dart", "Dart"},
    Entry{"el", "Emacs Lisp"},
    Entry{"erl", "Erlang"},
    Entry{"ex", "Elixir"},
    Entry{"exs", "Elixir"},
    Entry{"f90", "Fortran"},
    Entry{"go", "Go"},
    Entry{"groovy", "Groovy"},
    Entry{"h", "C"},
    Entry{"hh", "C++"},
    Entry{"hpp", "C++"},
    Entry{"hrl", "Erlang"},
    Entry{"hs", "Haskell"},
    Entry{"htm", "HTML"},
    Entry{"html", "HTML"},
    Entry{"hxx", "C++"},
    Entry{"ini", "INI"},
    Entry{"java", "Java"},
    Entry{"jl", "Julia"},
    Entry{"js", "JavaScript"},
    Entry{"json", "JSON"},
    Entry{"jsx", "JavaScript"},
    Entry{"kt", "Kotlin"},
    Entry{"kts", "Kotlin"},
    Entry{"less", "Less"},
    Entry{"lua", "Lua"},
    Entry{"m", "Objective-C"},
    Entry{"markdown", "Markdown"},
    Entry{"md", "Markdown"},
    Entry{"mjs", "JavaScript"},
    Entry{"ml", "OCaml"},
    Entry{"mli", "OCaml"},
    Entry{"mm", "Objective-C++"},
    Entry{"nim", "Nim"},
    Entry{"php", "PHP"},
    Entry{"pl", "Perl"},
    Entry{"pm", "Perl"},
    Entry{"proto", "Protocol Buffer"},
    Entry{"ps1", "PowerShell"},
    Entry{"py", "Python"},
    Entry{"pyi", "Python"},
    Entry{"r", "R"},
    Entry{"rb", "Ruby"},
    Entry{"rs", "Rust"},
    Entry{"rst", "reStructuredText"},
    Entry{"sass", "Sass"},
    Entry{"scala", "Scala"},
    Entry{"scm", "Scheme"},
    Entry{"scss", "SCSS"},
    Entry{"sh", "Shell"},
    Entry{"sql", "SQL"},
    Entry{"swift", "Swift"},
    Entry{"tex", "TeX"},
    Entry{"tf", "HCL"},
    Entry{"toml", "TOML"},
    Entry{"ts", "TypeScript"},
    Entry{"tsx", "TypeScript"},
    Entry{"vim", "Vim Script"},
    Entry{"xml", "XML"},
    Entry{"yaml", "YAML"},
    Entry{"yml", "YAML"},
    Entry{"zig", "Zig"});

}  // namespace

std::string_view LanguageForName(std::string_view name) {
  // An exact filename match wins (Makefile, Dockerfile, BUILD, ...).
  if (const auto it = kFilenameLanguages.find(name); it != kFilenameLanguages.end()) {
    return it->second;
  }
  // Otherwise the extension, using the same notion as the {ext} field: `.ext` or "" (a dotfile
  // such as `.bashrc` has none). Drop the dot and fold case for the lookup.
  const std::string ext = stdfs::path(std::string(name)).extension().string();
  if (ext.size() <= 1) {
    return "";
  }
  const std::string key = absl::AsciiStrToLower(ext.substr(1));
  const auto it = kExtensionLanguages.find(std::string_view(key));
  return it == kExtensionLanguages.end() ? std::string_view() : it->second;
}

}  // namespace xff::language
