// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/matching/similarity/similarity.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_join.h"

namespace xff::similarity {
namespace {

std::vector<std::string> Words(std::string_view text) {
  std::vector<std::string> words;
  std::string word;
  for (const char value : text) {
    const auto byte = static_cast<unsigned char>(value);
    if (byte >= 0x80 || absl::ascii_isalnum(value)) {
      word.push_back(absl::ascii_tolower(value));
    } else if (!word.empty()) {
      words.push_back(std::move(word));
      word.clear();
    }
  }
  if (!word.empty()) {
    words.push_back(std::move(word));
  }
  return words;
}

absl::flat_hash_set<std::string> Shingles(std::string_view text, std::size_t width) {
  const std::vector<std::string> words = Words(text);
  absl::flat_hash_set<std::string> result;
  if (words.empty() || width == 0) {
    return result;
  }
  const std::size_t shingle_width = std::min(width, words.size());
  for (std::size_t begin = 0; begin + shingle_width <= words.size(); ++begin) {
    result.insert(
        absl::StrJoin(
            words.begin() + static_cast<std::ptrdiff_t>(begin),
            words.begin() + static_cast<std::ptrdiff_t>(begin + shingle_width), std::string_view("\0", 1)));
  }
  return result;
}

}  // namespace

int WordShinglePercent(std::string_view lhs, std::string_view rhs, std::size_t width) {
  if (width == 0) {
    return 0;
  }
  const absl::flat_hash_set<std::string> lhs_shingles = Shingles(lhs, width);
  const absl::flat_hash_set<std::string> rhs_shingles = Shingles(rhs, width);
  if (lhs_shingles.empty() && rhs_shingles.empty()) {
    return 100;
  }
  std::size_t intersection = 0;
  for (const std::string& shingle : lhs_shingles) {
    intersection += rhs_shingles.contains(shingle) ? 1 : 0;
  }
  const std::size_t union_size = lhs_shingles.size() + rhs_shingles.size() - intersection;
  return static_cast<int>(((100 * intersection) + (union_size / 2)) / union_size);
}

}  // namespace xff::similarity
