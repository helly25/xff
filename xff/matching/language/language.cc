// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/matching/language/language.h"

#include <array>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "mbo/status/status_macros.h"
#include "mbo/types/optional_ref.h"
#include "nlohmann/json.hpp"
#include "xff/matching/language/language_database_api.h"

namespace xff::language {
namespace {

using Json = nlohmann::ordered_json;

constexpr auto kStringFields = std::to_array<std::string_view>({"type", "color", "group", "source"});

struct Vocabulary {
  struct Record {
    std::string_view type;
    std::string_view color;
    std::string_view group;
    std::string_view source;
    std::vector<std::string_view> aliases;
    std::vector<std::string_view> extensions;
    std::vector<std::string_view> filenames;
  };

  std::map<std::string, Record, std::less<>> languages;
  std::map<std::string, std::string, std::less<>> extensions;
  std::map<std::string, std::string, std::less<>> filenames;
  std::vector<LanguageInfo> views;
  std::vector<std::unique_ptr<const Json>> layers;
  std::vector<std::unique_ptr<const std::string>> normalized;
};

struct State {
  absl::Mutex mutex;
  std::vector<std::unique_ptr<const Vocabulary>> snapshots ABSL_GUARDED_BY(mutex);
  mbo::types::OptionalRef<const Vocabulary> active ABSL_GUARDED_BY(mutex);
};

State& GlobalState() {
  static State state;
  return state;
}

std::string Lower(std::string_view value) {
  return absl::AsciiStrToLower(std::string(value));
}

void AddCore(Vocabulary& vocabulary, std::string_view extension, std::string_view language) {
  Vocabulary::Record& info = vocabulary.languages[std::string(language)];
  info.extensions.emplace_back(extension);
  vocabulary.extensions[std::string(extension)] = language;
}

void AddCoreFilename(Vocabulary& vocabulary, std::string_view filename, std::string_view language) {
  Vocabulary::Record& info = vocabulary.languages[std::string(language)];
  info.filenames.emplace_back(filename);
  vocabulary.filenames[std::string(filename)] = language;
}

Vocabulary CoreVocabulary() {
  Vocabulary vocabulary;
  constexpr auto kExtensions = std::to_array<std::pair<std::string_view, std::string_view>>({
      {"asm", "Assembly"},
      {"awk", "Awk"},
      {"bash", "Shell"},
      {"bat", "Batchfile"},
      {"bzl", "Starlark"},
      {"c", "C"},
      {"cc", "C++"},
      {"clj", "Clojure"},
      {"cljc", "Clojure"},
      {"cljs", "Clojure"},
      {"cmake", "CMake"},
      {"cpp", "C++"},
      {"cs", "C#"},
      {"css", "CSS"},
      {"cxx", "C++"},
      {"d", "D"},
      {"dart", "Dart"},
      {"el", "Emacs Lisp"},
      {"erl", "Erlang"},
      {"ex", "Elixir"},
      {"exs", "Elixir"},
      {"f90", "Fortran"},
      {"go", "Go"},
      {"groovy", "Groovy"},
      {"h", "C"},
      {"hh", "C++"},
      {"hpp", "C++"},
      {"hrl", "Erlang"},
      {"hs", "Haskell"},
      {"htm", "HTML"},
      {"html", "HTML"},
      {"hxx", "C++"},
      {"ini", "INI"},
      {"java", "Java"},
      {"jl", "Julia"},
      {"js", "JavaScript"},
      {"json", "JSON"},
      {"jsx", "JavaScript"},
      {"kt", "Kotlin"},
      {"kts", "Kotlin"},
      {"less", "Less"},
      {"lua", "Lua"},
      {"m", "Objective-C"},
      {"markdown", "Markdown"},
      {"md", "Markdown"},
      {"mjs", "JavaScript"},
      {"ml", "OCaml"},
      {"mli", "OCaml"},
      {"mm", "Objective-C++"},
      {"nim", "Nim"},
      {"php", "PHP"},
      {"pl", "Perl"},
      {"pm", "Perl"},
      {"proto", "Protocol Buffer"},
      {"ps1", "PowerShell"},
      {"py", "Python"},
      {"pyi", "Python"},
      {"r", "R"},
      {"rb", "Ruby"},
      {"rs", "Rust"},
      {"rst", "reStructuredText"},
      {"sass", "Sass"},
      {"scala", "Scala"},
      {"scm", "Scheme"},
      {"scss", "SCSS"},
      {"sh", "Shell"},
      {"sql", "SQL"},
      {"swift", "Swift"},
      {"tex", "TeX"},
      {"tf", "HCL"},
      {"toml", "TOML"},
      {"ts", "TypeScript"},
      {"tsx", "TypeScript"},
      {"vim", "Vim Script"},
      {"xml", "XML"},
      {"yaml", "YAML"},
      {"yml", "YAML"},
      {"zig", "Zig"},
  });
  for (const auto& [extension, language] : kExtensions) {
    AddCore(vocabulary, extension, language);
  }
  constexpr auto kFilenames = std::to_array<std::pair<std::string_view, std::string_view>>({
      {".bash_profile", "Shell"},
      {".bashrc", "Shell"},
      {".profile", "Shell"},
      {".zshrc", "Shell"},
      {"BUILD", "Starlark"},
      {"BUILD.bazel", "Starlark"},
      {"CMakeLists.txt", "CMake"},
      {"Dockerfile", "Dockerfile"},
      {"GNUmakefile", "Makefile"},
      {"Gemfile", "Ruby"},
      {"MODULE.bazel", "Starlark"},
      {"Makefile", "Makefile"},
      {"Rakefile", "Ruby"},
      {"WORKSPACE", "Starlark"},
      {"WORKSPACE.bazel", "Starlark"},
      {"makefile", "Makefile"},
  });
  for (const auto& [filename, language] : kFilenames) {
    AddCoreFilename(vocabulary, filename, language);
  }
  return vocabulary;
}

absl::Status JsonError(std::string_view layer, std::string_view message) {
  return absl::InvalidArgumentError(absl::StrCat("language vocabulary '", layer, "': ", message));
}

absl::StatusOr<std::vector<std::string_view>> StringList(
    const Json& object,
    std::string_view field,
    std::string_view layer,
    std::string_view language) {
  const auto found = object.find(field);
  if (found == object.end()) {
    return std::vector<std::string_view>{};
  }
  if (!found->is_array()) {
    return JsonError(layer, absl::StrCat(language, ".", field, " must be an array of strings"));
  }
  std::vector<std::string_view> result;
  for (const Json& value : *found) {
    if (!value.is_string()) {
      return JsonError(layer, absl::StrCat(language, ".", field, " must contain only strings"));
    }
    result.push_back(value.get_ref<const std::string&>());
  }
  return result;
}

absl::Status ApplyStringFields(
    Vocabulary::Record& info,
    const Json& value,
    std::string_view layer,
    std::string_view name) {
  for (const std::string_view field : kStringFields) {
    const auto found = value.find(field);
    if (found == value.end()) {
      continue;
    }
    if (!found->is_string()) {
      return JsonError(layer, absl::StrCat(name, ".", field, " must be a string"));
    }
    const std::string_view field_value = found->get_ref<const std::string&>();
    if (field == "type") {
      info.type = field_value;
    } else if (field == "color") {
      info.color = field_value;
    } else if (field == "group") {
      info.group = field_value;
    } else {
      info.source = field_value;
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<std::string_view>> Filenames(
    const Json& value,
    std::string_view layer,
    std::string_view name) {
  MBO_ASSIGN_OR_RETURN(auto filenames, StringList(value, "filenames", layer, name));
  for (const std::string_view filename : filenames) {
    if (filename.empty() || absl::StrContains(filename, '/')) {
      return JsonError(layer, absl::StrCat(name, " has invalid filename '", filename, "'"));
    }
  }
  return filenames;
}

absl::Status ApplyAliases(Vocabulary::Record& info, const Json& value, std::string_view layer, std::string_view name) {
  if (!value.contains("aliases")) {
    return absl::OkStatus();
  }
  MBO_ASSIGN_OR_RETURN(info.aliases, StringList(value, "aliases", layer, name));
  return absl::OkStatus();
}

class LayerProcessor {
 public:
  LayerProcessor(Vocabulary& vocabulary, std::string_view layer, ConflictPolicy conflicts)
      : vocabulary_(vocabulary), layer_(layer), conflicts_(conflicts) {}

  absl::Status Apply(std::string_view text) {
    root_ = std::make_unique<Json>(Json::parse(text, nullptr, false));
    if (root_->is_discarded()) {
      return JsonError(layer_, "invalid JSON");
    }
    if (!root_->is_object()) {
      return JsonError(layer_, "top level must be an object keyed by canonical language name");
    }
    for (const auto& [name, value] : root_->items()) {
      MBO_RETURN_IF_ERROR(ApplyEntry(name, value));
    }
    Commit();
    return absl::OkStatus();
  }

 private:
  using Claims = std::map<std::string, std::string, std::less<>>;
  using Replacements = std::set<std::string, std::less<>>;

  absl::Status AddClaims(
      Claims& claims,
      absl::Span<const std::string_view> values,
      std::string_view language,
      std::string_view kind) {
    for (const std::string_view value : values) {
      const auto [claim, inserted] = claims.emplace(value, language);
      if (inserted || claim->second == language) {
        continue;
      }
      if (conflicts_ == ConflictPolicy::kError) {
        return JsonError(layer_, absl::StrCat(kind, " '", value, "' is claimed by ", claim->second, " and ", language));
      }
      if (conflicts_ == ConflictPolicy::kLast) {
        claim->second = language;
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<std::vector<std::string_view>> Extensions(const Json& value, std::string_view name) {
    MBO_ASSIGN_OR_RETURN(auto extensions, StringList(value, "extensions", layer_, name));
    for (std::string_view& extension : extensions) {
      std::string normalized = Lower(extension);
      if (normalized.starts_with('.')) {
        normalized.erase(0, 1);
      }
      if (normalized.empty() || absl::StrContains(normalized, '/')) {
        return JsonError(layer_, absl::StrCat(name, " has invalid extension '", normalized, "'"));
      }
      if (normalized != extension) {
        normalized_.push_back(std::make_unique<const std::string>(std::move(normalized)));
        extension = *normalized_.back();
      }
    }
    return extensions;
  }

  absl::Status ApplyExtensionClaims(Vocabulary::Record& info, const Json& value, std::string_view name) {
    if (!value.contains("extensions")) {
      return absl::OkStatus();
    }
    MBO_ASSIGN_OR_RETURN(info.extensions, Extensions(value, name));
    replaced_extensions_.insert(std::string(name));
    return AddClaims(extension_claims_, info.extensions, name, "extension");
  }

  absl::Status ApplyFilenameClaims(Vocabulary::Record& info, const Json& value, std::string_view name) {
    if (!value.contains("filenames")) {
      return absl::OkStatus();
    }
    MBO_ASSIGN_OR_RETURN(info.filenames, Filenames(value, layer_, name));
    replaced_filenames_.insert(std::string(name));
    return AddClaims(filename_claims_, info.filenames, name, "filename");
  }

  absl::Status ApplyEntry(std::string_view name, const Json& value) {
    if (name.empty() || !value.is_object()) {
      return JsonError(layer_, absl::StrCat("invalid language entry: ", name));
    }
    Vocabulary::Record info;
    if (const auto found = vocabulary_.languages.find(name); found != vocabulary_.languages.end()) {
      info = found->second;
    }
    MBO_RETURN_IF_ERROR(ApplyStringFields(info, value, layer_, name));
    MBO_RETURN_IF_ERROR(ApplyAliases(info, value, layer_, name));
    MBO_RETURN_IF_ERROR(ApplyExtensionClaims(info, value, name));
    MBO_RETURN_IF_ERROR(ApplyFilenameClaims(info, value, name));
    languages_[std::string(name)] = std::move(info);
    return absl::OkStatus();
  }

  void Commit() {
    for (const std::string& name : replaced_extensions_) {
      std::erase_if(vocabulary_.extensions, [&](const auto& claim) { return claim.second == name; });
    }
    for (const std::string& name : replaced_filenames_) {
      std::erase_if(vocabulary_.filenames, [&](const auto& claim) { return claim.second == name; });
    }
    for (auto& [name, info] : languages_) {
      vocabulary_.languages[name] = std::move(info);
    }
    for (const auto& [extension, name] : extension_claims_) {
      vocabulary_.extensions[extension] = name;
    }
    for (const auto& [filename, name] : filename_claims_) {
      vocabulary_.filenames[filename] = name;
    }
    for (auto& value : normalized_) {
      vocabulary_.normalized.push_back(std::move(value));
    }
    vocabulary_.layers.push_back(std::move(root_));
  }

  Vocabulary& vocabulary_;
  std::string_view layer_;
  ConflictPolicy conflicts_;
  Claims extension_claims_;
  Claims filename_claims_;
  std::map<std::string, Vocabulary::Record, std::less<>> languages_;
  Replacements replaced_extensions_;
  Replacements replaced_filenames_;
  std::unique_ptr<const Json> root_;
  std::vector<std::unique_ptr<const std::string>> normalized_;
};

absl::StatusOr<std::string> ReadFile(const std::string& path) {
  const std::ifstream input(path);
  if (!input) {
    return absl::NotFoundError(absl::StrCat("cannot read language vocabulary: ", path));
  }
  std::ostringstream text;
  text << input.rdbuf();
  return std::move(text).str();
}

struct LookupResult {
  std::string_view name;
  mbo::types::OptionalRef<const Vocabulary::Record> record;
};

LookupResult Lookup(const Vocabulary& vocabulary, std::string_view name) {
  if (const auto found = vocabulary.filenames.find(name); found != vocabulary.filenames.end()) {
    return {.name = found->second, .record = vocabulary.languages.at(found->second)};
  }
  const std::string folded = Lower(name);
  for (std::size_t dot = folded.find('.'); dot != std::string::npos; dot = folded.find('.', dot + 1)) {
    if (dot + 1 == folded.size()) {
      continue;
    }
    const auto found = vocabulary.extensions.find(std::string_view(folded).substr(dot + 1));
    if (found != vocabulary.extensions.end()) {
      return {.name = found->second, .record = vocabulary.languages.at(found->second)};
    }
  }
  return {};
}

LanguageInfo View(std::string_view name, const Vocabulary::Record& record) {
  return {
      .name = name,
      .type = record.type,
      .color = record.color,
      .group = record.group,
      .source = record.source,
      .aliases = record.aliases,
      .extensions = record.extensions,
      .filenames = record.filenames,
  };
}

void Finalize(Vocabulary& vocabulary) {
  vocabulary.views.reserve(vocabulary.languages.size());
  for (const auto& [name, record] : vocabulary.languages) {
    vocabulary.views.push_back(View(name, record));
  }
}

void EnsureConfigured(State& state) ABSL_EXCLUSIVE_LOCKS_REQUIRED(state.mutex) {
  if (state.active.has_value()) {
    return;
  }
  auto vocabulary = std::make_unique<Vocabulary>(CoreVocabulary());
  for (const Database& database : Databases()) {
    CHECK_OK(LayerProcessor(*vocabulary, database.name, ConflictPolicy::kLast).Apply(database.json()));
  }
  Finalize(*vocabulary);
  state.snapshots.push_back(std::move(vocabulary));
  state.active.set_ref(*state.snapshots.back());
}

}  // namespace

absl::Status Configure(absl::Span<const std::string> files, ConflictPolicy conflicts) {
  if (files.empty()) {
    State& state = GlobalState();
    const absl::MutexLock lock(&state.mutex);
    state.active.reset();
    return absl::OkStatus();
  }
  Vocabulary vocabulary = CoreVocabulary();
  for (const Database& database : Databases()) {
    MBO_RETURN_IF_ERROR(LayerProcessor(vocabulary, database.name, ConflictPolicy::kLast).Apply(database.json()));
  }
  for (const std::string& file : files) {
    MBO_ASSIGN_OR_RETURN(const std::string text, ReadFile(file));
    MBO_RETURN_IF_ERROR(LayerProcessor(vocabulary, file, conflicts).Apply(text));
  }
  Finalize(vocabulary);
  State& state = GlobalState();
  const absl::MutexLock lock(&state.mutex);
  auto snapshot = std::make_unique<const Vocabulary>(std::move(vocabulary));
  state.snapshots.push_back(std::move(snapshot));
  state.active.set_ref(*state.snapshots.back());
  return absl::OkStatus();
}

std::optional<LanguageInfo> InfoForName(std::string_view name) {
  State& state = GlobalState();
  const absl::MutexLock lock(&state.mutex);
  EnsureConfigured(state);
  const LookupResult found = Lookup(*state.active, name);
  return found.record.has_value() ? std::optional<LanguageInfo>(View(found.name, *found.record)) : std::nullopt;
}

std::string_view LanguageForName(std::string_view name) {
  State& state = GlobalState();
  const absl::MutexLock lock(&state.mutex);
  EnsureConfigured(state);
  return Lookup(*state.active, name).name;
}

absl::Span<const LanguageInfo> Languages() {
  State& state = GlobalState();
  const absl::MutexLock lock(&state.mutex);
  EnsureConfigured(state);
  return state.active->views;
}

}  // namespace xff::language
