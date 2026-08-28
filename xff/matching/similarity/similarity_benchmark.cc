// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

// Reproducible cost model for the exact word-shingle verifier:
//
//   bazel run -c opt //xff/matching/similarity:similarity_benchmark

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "xff/matching/similarity/similarity.h"

namespace xff::similarity {
namespace {

// NOLINTBEGIN(*-magic-numbers)

std::string Document(std::size_t words, std::size_t changed_every) {
  std::string result;
  result.reserve(words * 12);
  for (std::size_t index = 0; index < words; ++index) {
    if (changed_every != 0 && index % changed_every == 0) {
      absl::StrAppend(&result, "changed", index, " ");
    } else {
      absl::StrAppend(&result, "token", index % 257, " ");
    }
  }
  return result;
}

void BmWordShinglePercent(benchmark::State& state, std::size_t changed_every) {
  const auto words = static_cast<std::size_t>(state.range(0));
  const auto width = static_cast<std::size_t>(state.range(1));
  const std::string lhs = Document(words, 0);
  const std::string rhs = Document(words, changed_every);
  for (auto unused : state) {
    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
    benchmark::DoNotOptimize(WordShinglePercent(lhs, rhs, width));
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(words * 2));
}

// XFF_ABI_POINTER: google-benchmark registration API requires a mutable benchmark object.
void Args(benchmark::Benchmark* benchmark) {
  static constexpr std::array kWordCounts = std::to_array<std::int64_t>({32, 256, 2'048});
  static constexpr std::array kWidths = std::to_array<std::int64_t>({1, 3, 5});
  for (const std::int64_t words : kWordCounts) {
    for (const std::int64_t width : kWidths) {
      benchmark->Args({words, width});
    }
  }
}

BENCHMARK_CAPTURE(BmWordShinglePercent, equal, 0)->Apply(Args);
BENCHMARK_CAPTURE(BmWordShinglePercent, nearby, 16)->Apply(Args);
BENCHMARK_CAPTURE(BmWordShinglePercent, disjoint, 1)->Apply(Args);

// NOLINTEND(*-magic-numbers)

}  // namespace
}  // namespace xff::similarity
