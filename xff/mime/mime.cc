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

#include "xff/mime/mime.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/ascii.h"
#include "mbo/container/limited_map.h"

namespace xff::mime {
namespace {

namespace stdfs = std::filesystem;

using ExtType = std::pair<std::string_view, std::string_view>;

// Lowercase extension (no dot) -> media type, sorted by extension for the constexpr
// LimitedMap. Curated to common, mostly IANA-registered types; source-code
// extensions are deliberately omitted (language classification is a separate
// concern -- github-linguist, a future feature). Unknown extensions fall back to
// application/octet-stream in TypeForName, so they need no entry here.
constexpr auto kExtensionTypes = mbo::container::MakeLimitedMap(
    ExtType{"7z", "application/x-7z-compressed"},
    ExtType{"avi", "video/x-msvideo"},
    ExtType{"bmp", "image/bmp"},
    ExtType{"bz2", "application/x-bzip2"},
    ExtType{"css", "text/css"},
    ExtType{"csv", "text/csv"},
    ExtType{"flac", "audio/flac"},
    ExtType{"gif", "image/gif"},
    ExtType{"gz", "application/gzip"},
    ExtType{"htm", "text/html"},
    ExtType{"html", "text/html"},
    ExtType{"ico", "image/vnd.microsoft.icon"},
    ExtType{"jpeg", "image/jpeg"},
    ExtType{"jpg", "image/jpeg"},
    ExtType{"js", "text/javascript"},
    ExtType{"json", "application/json"},
    ExtType{"md", "text/markdown"},
    ExtType{"mkv", "video/x-matroska"},
    ExtType{"mov", "video/quicktime"},
    ExtType{"mp3", "audio/mpeg"},
    ExtType{"mp4", "video/mp4"},
    ExtType{"ogg", "audio/ogg"},
    ExtType{"otf", "font/otf"},
    ExtType{"pdf", "application/pdf"},
    ExtType{"png", "image/png"},
    ExtType{"rar", "application/vnd.rar"},
    ExtType{"svg", "image/svg+xml"},
    ExtType{"tar", "application/x-tar"},
    ExtType{"tif", "image/tiff"},
    ExtType{"tiff", "image/tiff"},
    ExtType{"toml", "application/toml"},
    ExtType{"ttf", "font/ttf"},
    ExtType{"txt", "text/plain"},
    ExtType{"wasm", "application/wasm"},
    ExtType{"wav", "audio/wav"},
    ExtType{"webm", "video/webm"},
    ExtType{"webp", "image/webp"},
    ExtType{"woff", "font/woff"},
    ExtType{"woff2", "font/woff2"},
    ExtType{"xml", "application/xml"},
    ExtType{"xz", "application/x-xz"},
    ExtType{"yaml", "application/yaml"},
    ExtType{"yml", "application/yaml"},
    ExtType{"zip", "application/zip"});

}  // namespace

std::string_view TypeForName(std::string_view name) {
  constexpr std::string_view kFallback = "application/octet-stream";
  // Use the same extension notion as the {ext} field: `.ext` or "" (a dotfile such
  // as `.bashrc` has none). Drop the dot and fold case for the lookup.
  const std::string ext = stdfs::path(std::string(name)).extension().string();
  if (ext.size() <= 1) {
    return kFallback;
  }
  const std::string key = absl::AsciiStrToLower(ext.substr(1));
  const auto it = kExtensionTypes.find(std::string_view(key));
  return it == kExtensionTypes.end() ? kFallback : it->second;
}

}  // namespace xff::mime
