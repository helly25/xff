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

#include "xff/archive/archive_filters.h"

#include <archive.h>

#include <algorithm>
#include <array>
#include <span>

namespace xff::archive {
namespace {

// Internal/self-contained filters plus the codec libraries explicitly linked by @libarchive in
// this module. PROGRAM, GRZIP, LRZIP, and LZOP stay out: this build handles those by launching a
// host executable, which violates xff's standalone-binary contract. A future Brotli filter stays
// out until explicitly delegated to @xff_brotli.
constexpr std::array kNativeFilterCodes = {
    ARCHIVE_FILTER_NONE, ARCHIVE_FILTER_GZIP, ARCHIVE_FILTER_BZIP2, ARCHIVE_FILTER_COMPRESS,
    ARCHIVE_FILTER_LZMA, ARCHIVE_FILTER_XZ,   ARCHIVE_FILTER_UU,    ARCHIVE_FILTER_RPM,
    ARCHIVE_FILTER_LZIP, ARCHIVE_FILTER_LZ4,  ARCHIVE_FILTER_ZSTD,
};

}  // namespace

std::span<const int> NativeFilterCodes() {
  return kNativeFilterCodes;
}

bool internal::EnableNativeFiltersWith(struct ::archive* handle, const FilterRegistrar registrar) {
  if (handle == nullptr) {
    return false;
  }
  return std::ranges::all_of(
      kNativeFilterCodes, [handle, registrar](const int code) { return registrar(handle, code) == ARCHIVE_OK; });
}

bool EnableNativeFilters(struct ::archive* handle) {
  return internal::EnableNativeFiltersWith(handle, ::archive_read_support_filter_by_code);
}

}  // namespace xff::archive
