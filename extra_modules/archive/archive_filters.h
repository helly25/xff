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

#ifndef XFF_ARCHIVE_ARCHIVE_FILTERS_H_
#define XFF_ARCHIVE_ARCHIVE_FILTERS_H_

#include <span>

struct archive;

namespace xff::archive {

namespace internal {

using FilterRegistrar = int (*)(struct ::archive*, int);

// Test seam for the failure contract: production always supplies libarchive's registrar, while a
// unit test can reject one code without depending on a broken libarchive build.
bool EnableNativeFiltersWith(struct ::archive* handle, FilterRegistrar registrar);

}  // namespace internal

// The exact libarchive read filters xff enables. This is deliberately an allowlist rather than
// `archive_read_support_filter_all`: the latter can invoke host programs for codecs not linked into
// the standalone binary, and would silently absorb future libarchive filters such as Brotli. Brotli
// belongs to its separately removable extra and must not appear here.
std::span<const int> NativeFilterCodes();

// Registers every NativeFilterCodes() entry. Returns false if this build cannot provide one of the
// filters xff promises; callers must not continue with a partially configured reader.
bool EnableNativeFilters(struct ::archive* handle);

}  // namespace xff::archive

#endif  // XFF_ARCHIVE_ARCHIVE_FILTERS_H_
