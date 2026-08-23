// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/matching/mime/mime.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "mbo/status/status_macros.h"
#include "nlohmann/json.hpp"
#include "xff/matching/mime/database.h"

namespace xff::mime {
namespace {

namespace stdfs = std::filesystem;
// Object member order gives `--mime-conflicts=first|last` an observable, useful
// meaning. JSON itself does not assign semantic significance to that order, but
// nlohmann preserves the source order here rather than silently sorting keys.
using Json = nlohmann::ordered_json;

constexpr std::string_view kFallback = "application/octet-stream";
constexpr auto kStringFields = std::to_array<std::string_view>({"description", "source", "charset"});

struct Vocabulary {
  std::map<std::string, TypeInfo, std::less<>> types;
  std::map<std::string, std::string, std::less<>> extensions;
};

struct State {
  absl::Mutex mutex;
  Vocabulary vocabulary ABSL_GUARDED_BY(mutex);
};

State& GlobalState() {
  static State state;
  return state;
}

std::string Lower(std::string_view value) {
  return absl::AsciiStrToLower(std::string(value));
}

void AddCore(Vocabulary& vocabulary, std::string_view extension, std::string_view type) {
  TypeInfo& info = vocabulary.types[std::string(type)];
  info.type = type;
  info.extensions.emplace_back(extension);
  vocabulary.extensions[std::string(extension)] = type;
}

Vocabulary CoreVocabulary() {
  Vocabulary vocabulary;
  constexpr auto kEntries = std::to_array<std::pair<std::string_view, std::string_view>>({
      {"7z", "application/x-7z-compressed"},
      {"avi", "video/x-msvideo"},
      {"bmp", "image/bmp"},
      {"bz2", "application/x-bzip2"},
      {"css", "text/css"},
      {"csv", "text/csv"},
      {"flac", "audio/flac"},
      {"gif", "image/gif"},
      {"gz", "application/gzip"},
      {"htm", "text/html"},
      {"html", "text/html"},
      {"ico", "image/vnd.microsoft.icon"},
      {"jpeg", "image/jpeg"},
      {"jpg", "image/jpeg"},
      {"js", "text/javascript"},
      {"json", "application/json"},
      {"md", "text/markdown"},
      {"mkv", "video/x-matroska"},
      {"mov", "video/quicktime"},
      {"mp3", "audio/mpeg"},
      {"mp4", "video/mp4"},
      {"ogg", "audio/ogg"},
      {"otf", "font/otf"},
      {"pdf", "application/pdf"},
      {"png", "image/png"},
      {"rar", "application/vnd.rar"},
      {"svg", "image/svg+xml"},
      {"tar", "application/x-tar"},
      {"tif", "image/tiff"},
      {"tiff", "image/tiff"},
      {"toml", "application/toml"},
      {"ttf", "font/ttf"},
      {"txt", "text/plain"},
      {"wasm", "application/wasm"},
      {"wav", "audio/wav"},
      {"webm", "video/webm"},
      {"webp", "image/webp"},
      {"woff", "font/woff"},
      {"woff2", "font/woff2"},
      {"xml", "application/xml"},
      {"xz", "application/x-xz"},
      {"yaml", "application/yaml"},
      {"yml", "application/yaml"},
      {"zip", "application/zip"},
  });
  for (const auto& [extension, type] : kEntries) {
    AddCore(vocabulary, extension, type);
  }
  vocabulary.types[std::string(kFallback)].type = kFallback;
  return vocabulary;
}

absl::Status JsonError(std::string_view layer, std::string_view message) {
  return absl::InvalidArgumentError(absl::StrCat("MIME vocabulary '", layer, "': ", message));
}

absl::StatusOr<std::vector<std::string>> StringList(
    const Json& object,
    std::string_view field,
    std::string_view layer,
    std::string_view type) {
  const auto found = object.find(field);
  if (found == object.end()) {
    return std::vector<std::string>{};
  }
  if (!found->is_array()) {
    return JsonError(layer, absl::StrCat(type, ".", field, " must be an array of strings"));
  }
  std::vector<std::string> result;
  for (const Json& value : *found) {
    if (!value.is_string()) {
      return JsonError(layer, absl::StrCat(type, ".", field, " must contain only strings"));
    }
    result.push_back(value.get<std::string>());
  }
  return result;
}

absl::Status ApplyStringFields(TypeInfo& info, const Json& value, std::string_view layer, std::string_view type) {
  for (const std::string_view field : kStringFields) {
    const auto found = value.find(field);
    if (found == value.end()) {
      continue;
    }
    if (!found->is_string()) {
      return JsonError(layer, absl::StrCat(type, ".", field, " must be a string"));
    }
    const std::string field_value = found->get<std::string>();
    if (field == "description") {
      info.description = field_value;
    } else if (field == "source") {
      info.source = field_value;
    } else {
      info.charset = field_value;
    }
  }
  return absl::OkStatus();
}

absl::Status ApplyOptionalFields(TypeInfo& info, const Json& value, std::string_view layer, std::string_view type) {
  if (const auto found = value.find("compressible"); found != value.end()) {
    if (!found->is_boolean()) {
      return JsonError(layer, absl::StrCat(type, ".compressible must be boolean"));
    }
    info.compressible = found->get<bool>();
  }
  if (value.contains("aliases")) {
    MBO_ASSIGN_OR_RETURN(info.aliases, StringList(value, "aliases", layer, type));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<std::string>> Extensions(const Json& value, std::string_view layer, std::string_view type) {
  MBO_ASSIGN_OR_RETURN(auto extensions, StringList(value, "extensions", layer, type));
  for (std::string& extension : extensions) {
    extension = Lower(extension);
    if (extension.starts_with('.')) {
      extension.erase(0, 1);
    }
    if (extension.empty() || absl::StrContains(extension, '/')) {
      return JsonError(layer, absl::StrCat(type, " has invalid extension '", extension, "'"));
    }
  }
  return extensions;
}

absl::Status AddClaims(
    std::map<std::string, std::string, std::less<>>& claims,
    absl::Span<const std::string> extensions,
    std::string_view type,
    std::string_view layer,
    ConflictPolicy conflicts) {
  for (const std::string& extension : extensions) {
    const auto [claim, inserted] = claims.emplace(extension, type);
    if (inserted || claim->second == type) {
      continue;
    }
    if (conflicts == ConflictPolicy::kError) {
      return JsonError(layer, absl::StrCat("extension '", extension, "' is claimed by ", claim->second, " and ", type));
    }
    if (conflicts == ConflictPolicy::kLast) {
      claim->second = type;
    }
  }
  return absl::OkStatus();
}

absl::Status ApplyEntry(
    Vocabulary& vocabulary,
    std::map<std::string, std::string, std::less<>>& claims,
    std::set<std::string, std::less<>>& replaced_types,
    std::string_view raw_type,
    const Json& value,
    std::string_view layer,
    ConflictPolicy conflicts) {
  const std::string type = Lower(raw_type);
  if (!absl::StrContains(type, '/') || !value.is_object()) {
    return JsonError(layer, absl::StrCat("invalid media-type entry: ", raw_type));
  }
  TypeInfo info = vocabulary.types.contains(type) ? vocabulary.types.at(type) : TypeInfo{};
  info.type = type;
  MBO_RETURN_IF_ERROR(ApplyStringFields(info, value, layer, type));
  MBO_RETURN_IF_ERROR(ApplyOptionalFields(info, value, layer, type));
  if (value.contains("extensions")) {
    MBO_ASSIGN_OR_RETURN(const auto extensions, Extensions(value, layer, type));
    info.extensions.clear();
    replaced_types.insert(type);
    MBO_RETURN_IF_ERROR(AddClaims(claims, extensions, type, layer, conflicts));
  }
  vocabulary.types[type] = std::move(info);
  return absl::OkStatus();
}

absl::Status ApplyLayer(
    Vocabulary& vocabulary,
    std::string_view text,
    std::string_view layer,
    ConflictPolicy conflicts) {
  const Json root = Json::parse(text, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded()) {
    return JsonError(layer, "invalid JSON");
  }
  if (!root.is_object()) {
    return JsonError(layer, "top level must be an object keyed by media type");
  }

  std::map<std::string, std::string, std::less<>> claims;
  std::set<std::string, std::less<>> replaced_types;
  for (const auto& [raw_type, value] : root.items()) {
    MBO_RETURN_IF_ERROR(ApplyEntry(vocabulary, claims, replaced_types, raw_type, value, layer, conflicts));
  }

  for (auto it = vocabulary.extensions.begin(); it != vocabulary.extensions.end();) {
    it = replaced_types.contains(it->second) ? vocabulary.extensions.erase(it) : std::next(it);
  }
  for (const auto& [extension, type] : claims) {
    vocabulary.extensions[extension] = type;
    vocabulary.types[type].extensions.push_back(extension);
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> ReadFile(const std::string& path) {
  const std::ifstream input(path);
  if (!input) {
    return absl::NotFoundError(absl::StrCat("cannot read MIME vocabulary: ", path));
  }
  std::ostringstream text;
  text << input.rdbuf();
  return std::move(text).str();
}

TypeInfo Lookup(const Vocabulary& vocabulary, std::string_view name) {
  const std::string extension = stdfs::path(std::string(name)).extension().string();
  if (extension.size() > 1) {
    const auto found = vocabulary.extensions.find(Lower(std::string_view(extension).substr(1)));
    if (found != vocabulary.extensions.end()) {
      return vocabulary.types.at(found->second);
    }
  }
  return vocabulary.types.at(std::string(kFallback));
}

}  // namespace

std::string_view TypeInfo::Category() const {
  return std::string_view(type).substr(0, type.find('/'));
}

absl::Status Configure(absl::Span<const std::string> files, ConflictPolicy conflicts) {
  Vocabulary vocabulary = CoreVocabulary();
  for (const Database& database : Databases()) {
    MBO_RETURN_IF_ERROR(ApplyLayer(vocabulary, database.json, database.name, ConflictPolicy::kLast));
  }
  for (const std::string& file : files) {
    MBO_ASSIGN_OR_RETURN(const std::string text, ReadFile(file));
    MBO_RETURN_IF_ERROR(ApplyLayer(vocabulary, text, file, conflicts));
  }
  State& state = GlobalState();
  const absl::MutexLock lock(&state.mutex);
  state.vocabulary = std::move(vocabulary);
  return absl::OkStatus();
}

TypeInfo InfoForName(std::string_view name) {
  State& state = GlobalState();
  const absl::MutexLock lock(&state.mutex);
  if (state.vocabulary.types.empty()) {
    state.vocabulary = CoreVocabulary();
  }
  return Lookup(state.vocabulary, name);
}

std::string TypeForName(std::string_view name) {
  return InfoForName(name).type;
}

std::vector<TypeInfo> Types() {
  State& state = GlobalState();
  const absl::MutexLock lock(&state.mutex);
  if (state.vocabulary.types.empty()) {
    state.vocabulary = CoreVocabulary();
  }
  std::vector<TypeInfo> result;
  result.reserve(state.vocabulary.types.size());
  for (const auto& entry : state.vocabulary.types) {
    result.push_back(entry.second);
  }
  return result;
}

}  // namespace xff::mime
