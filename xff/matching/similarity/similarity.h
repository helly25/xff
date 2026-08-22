// SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com)
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_MATCHING_SIMILARITY_SIMILARITY_H_
#define XFF_MATCHING_SIMILARITY_SIMILARITY_H_

#include <cstddef>
#include <string_view>

namespace xff::similarity {

// Jaccard similarity of the unique contiguous word-shingle sets, rounded to an integer percentage
// in [0, 100]. Words are maximal runs of ASCII alphanumerics or UTF-8 bytes; ASCII case is folded,
// and punctuation/whitespace are separators. A non-empty input shorter than `width` contributes one
// shingle containing all its words, so short files remain meaningfully comparable. Width zero is
// invalid and returns 0.
[[nodiscard]] int WordShinglePercent(std::string_view lhs, std::string_view rhs, std::size_t width);

}  // namespace xff::similarity

#endif  // XFF_MATCHING_SIMILARITY_SIMILARITY_H_
