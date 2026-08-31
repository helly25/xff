// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
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

#ifndef XFF_ENV_ENV_H_
#define XFF_ENV_ENV_H_

#include <optional>
#include <string>
#include <string_view>

#include "absl/types/span.h"

// Centralized process-environment access. Every environment read in xff goes through this
// mutex-guarded, read-once cache rather than a scattered `std::getenv`. `getenv` is not thread
// safe against a concurrent `setenv`; xff never mutates the environment, and routing all reads
// here both serializes them and reads each variable at most once (the result - present or absent -
// is cached). This is the single `getenv` site in the codebase, so no other code needs a
// concurrency-mt-unsafe suppression.
namespace xff::env {

// The value of environment variable `name`, or nullopt when it is unset. Cached after the first
// lookup (misses too), so repeated reads never re-enter getenv. Thread-safe.
[[nodiscard]] std::optional<std::string> Get(std::string_view name);

// True when `name` is set (to any value, including empty); convenience over Get(name).has_value().
[[nodiscard]] bool Has(std::string_view name);

// Reads a fixed, known list of variables into the cache up front (e.g. at program start), so the
// later reads are pure cache hits. Optional - Get() caches lazily anyway - but it makes the set of
// variables the program consults explicit and reads them in one locked pass.
void Prewarm(absl::Span<const std::string_view> names);

// Test seam: force `name` to `value` (nullopt = unset) in the cache, bypassing getenv, so tests
// exercise env-dependent behavior without touching the real process environment. ClearForTesting
// drops every override and cached read. Not for production code.
void SetForTesting(std::string_view name, std::optional<std::string> value);
void ClearForTesting();

}  // namespace xff::env

#endif  // XFF_ENV_ENV_H_
