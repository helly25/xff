// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "benchmark/benchmark.h"
#include "xff/parser/parser.h"

namespace {

std::vector<std::string> MakeExpression(std::size_t predicates) {
  std::vector<std::string> args;
  args.reserve((2 * predicates) + 1);
  args.emplace_back(".");
  for (std::size_t i = 0; i < predicates; ++i) {
    args.emplace_back("-name");
    args.emplace_back(i % 2 == 0 ? "*.cc" : "*.h");
    if (i + 1 < predicates) {
      args.emplace_back("-a");
    }
  }
  return args;
}

void ParseExpression(benchmark::State& state) {
  const std::vector<std::string> args = MakeExpression(state.range(0));
  for (auto _ : state) {
    benchmark::DoNotOptimize(_);
    benchmark::DoNotOptimize(xff::parser::Parse(args));
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

}  // namespace

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  benchmark::RegisterBenchmark("ParseExpression", ParseExpression)
      ->Arg(1)
      ->Arg(4)
      ->Arg(16)
      ->Arg(64)
      ->Arg(256)
      ->Unit(benchmark::kMicrosecond);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
