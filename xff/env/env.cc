// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
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

#include "xff/env/env.h"

#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/btree_map.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"

namespace xff::env {
namespace {

// The process-wide env cache: `name -> value` where a nullopt value means known-unset. `mu` guards
// `entries`; a heterogeneous (std::less<>) comparator lets a string_view probe the string keys
// without allocating. This is the only state in the module.
struct Cache {
  absl::Mutex mu;
  absl::btree_map<std::string, std::optional<std::string>, std::less<>> entries ABSL_GUARDED_BY(mu);
};

// Meyers singleton: a function-local static is initialized thread-safely on first use and outlives
// every caller, avoiding any static-initialization-order concern.
Cache& Instance() {
  static Cache cache;
  return cache;
}

}  // namespace

std::optional<std::string> Get(std::string_view name) {
  Cache& cache = Instance();
  const absl::MutexLock lock(cache.mu);
  if (const auto it = cache.entries.find(name); it != cache.entries.end()) {
    return it->second;
  }
  // The sole getenv in the codebase: serialized by `cache.mu` and read at most once per name, so it
  // cannot race a (nonexistent) setenv. This centralization is precisely the mt-safety fix.
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* const raw = std::getenv(std::string(name).c_str());
  std::optional<std::string> value = raw == nullptr ? std::nullopt : std::make_optional<std::string>(raw);
  cache.entries.emplace(std::string(name), value);
  return value;
}

bool Has(std::string_view name) {
  return Get(name).has_value();
}

void Prewarm(absl::Span<const std::string_view> names) {
  for (const std::string_view name : names) {
    (void)Get(name);
  }
}

void SetForTesting(std::string_view name, std::optional<std::string> value) {
  Cache& cache = Instance();
  const absl::MutexLock lock(cache.mu);
  cache.entries.insert_or_assign(std::string(name), std::move(value));
}

void ClearForTesting() {
  Cache& cache = Instance();
  const absl::MutexLock lock(cache.mu);
  cache.entries.clear();
}

}  // namespace xff::env
