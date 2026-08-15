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

#include "xff/archive/member_cache.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "absl/synchronization/mutex.h"

namespace xff::archive {

std::optional<std::string> MemberCache::Get(std::string_view key) {
  const absl::MutexLock lock(mutex_);
  const auto found = index_.find(key);
  if (found == index_.end()) {
    return std::nullopt;
  }
  entries_.splice(entries_.begin(), entries_, found->second);
  return found->second->content;
}

void MemberCache::Put(std::string_view key, std::string_view content) {
  if (content.size() > capacity_) {
    return;  // never worth evicting everything else for
  }
  const absl::MutexLock lock(mutex_);
  const auto found = index_.find(key);
  if (found != index_.end()) {
    entries_.splice(entries_.begin(), entries_, found->second);
    size_ -= found->second->content.size();
    found->second->content = std::string(content);
    size_ += content.size();
    return;
  }
  while (size_ + content.size() > capacity_ && !entries_.empty()) {
    size_ -= entries_.back().content.size();
    index_.erase(entries_.back().key);
    entries_.pop_back();
  }
  entries_.push_front(Entry{.key = std::string(key), .content = std::string(content)});
  index_.emplace(std::string(key), entries_.begin());
  size_ += content.size();
}

std::size_t MemberCache::SizeBytes() const {
  const absl::MutexLock lock(mutex_);
  return size_;
}

}  // namespace xff::archive
