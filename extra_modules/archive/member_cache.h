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

#ifndef XFF_ARCHIVE_MEMBER_CACHE_H_
#define XFF_ARCHIVE_MEMBER_CACHE_H_

// A bounded LRU cache of extracted member content, one per open container. A composed expression
// reads the same member more than once (`-grep` + `{hash}` + `-cmp` each want the bytes), and every
// read is a fresh decompression pass through the container; the cache turns the repeats into copies.
//
// The byte cap is the point, not a tuning knob: cached content is decompressed content, so an
// unbounded cache is the decompression-bomb concern in a new hat. A member larger than the whole
// capacity is served but never stored.

#include <cstddef>
#include <list>
#include <optional>
#include <string>
#include <string_view>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

namespace xff::archive {

class MemberCache {
 public:
  // A quarter GiB default spread over one run's containers would be too much; per-container, the
  // common case is a handful of members re-read a handful of times.
  static constexpr std::size_t kDefaultCapacityBytes = std::size_t{64} << 20U;

  explicit MemberCache(std::size_t capacity_bytes = kDefaultCapacityBytes) : capacity_(capacity_bytes) {}

  // The cached content for `key`, refreshing its recency - or nullopt, and the caller extracts.
  [[nodiscard]] std::optional<std::string> Get(std::string_view key) ABSL_LOCKS_EXCLUDED(mutex_);

  // Stores `content` under `key`, evicting least-recently-used entries until it fits. Oversized
  // content (alone bigger than the capacity) is not stored. A key already present is refreshed.
  void Put(std::string_view key, std::string_view content) ABSL_LOCKS_EXCLUDED(mutex_);

  // The bytes currently held (content only), for tests and sizing decisions.
  [[nodiscard]] std::size_t SizeBytes() const ABSL_LOCKS_EXCLUDED(mutex_);

 private:
  struct Entry {
    std::string key;
    std::string content;
  };

  // `mutex_` guards `entries_`, `index_`, and `size_`: the list orders recency (front = most
  // recent), the map finds a list node by key, and `size_` is the held content bytes.
  mutable absl::Mutex mutex_;
  const std::size_t capacity_;
  std::list<Entry> entries_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<std::string, std::list<Entry>::iterator> index_ ABSL_GUARDED_BY(mutex_);
  std::size_t size_ ABSL_GUARDED_BY(mutex_) = 0;
};

}  // namespace xff::archive

#endif  // XFF_ARCHIVE_MEMBER_CACHE_H_
