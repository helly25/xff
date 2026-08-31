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

// Asserts that a sanitizer suppression file we point a `*_OPTIONS` env var at is actually THERE when
// the process runs.
//
// This is the piece nobody was testing, and the reason it matters is asymmetric: a sanitizer that
// reads suppressions treats a MISSING file as fatal (compiler-rt's
// `SuppressionContext::ParseFromFile` prints "failed to read suppressions file" and calls `Die()`,
// with only an exec-relative fallback), while MSan - which is where we wire it today - never reads it
// at all, so a broken path there is silent. Either way the delivery is what has to hold, and it is
// delivery this test checks: the path named in the environment must resolve from the test's working
// directory, which is its runfiles tree.
//
// The check is a no-op in an ordinary build, where no sanitizer env var is set. It has teeth exactly
// in the sanitizer cells, which is where the plumbing is used.

#include <array>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_split.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff {
namespace {

using ::testing::IsTrue;

// The sanitizer option variables that can carry `suppressions=`. TSan, LSan and UBSan DO consume it;
// MSan parses and ignores it (see .bazelrc), but the file still has to be deliverable, or turning the
// mechanism on for a tool that reads it would fail at the worst moment.
constexpr std::array kOptionVars = std::to_array<std::string_view>({
    "MSAN_OPTIONS",
    "TSAN_OPTIONS",
    "LSAN_OPTIONS",
    "UBSAN_OPTIONS",
});

// The `suppressions=` value inside a `key=value:key=value` options string, or empty when absent.
std::string SuppressionsPath(std::string_view options) {
  constexpr std::string_view kKey = "suppressions=";
  for (const std::string_view option : absl::StrSplit(options, ':')) {
    if (option.starts_with(kKey)) {
      return std::string(option.substr(kKey.size()));
    }
  }
  return "";
}

struct SuppressionsDeliveryTest : ::testing::Test {};

TEST_F(SuppressionsDeliveryTest, EverySuppressionFileNamedInTheEnvironmentIsPresent) {
  int checked = 0;
  for (const std::string_view var : kOptionVars) {
    // Bazel's own environment, read once in a single-threaded test; getenv is safe here.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char* const options = std::getenv(std::string(var).c_str());
    if (options == nullptr) {
      continue;
    }
    const std::string path = SuppressionsPath(options);
    if (path.empty()) {
      continue;
    }
    // Relative on purpose: a test runs with its runfiles tree as the working directory, and the
    // cc_test wrapper puts the file at that path. If this fails, a tool that actually reads
    // suppressions would die on startup instead.
    const std::ifstream file(path);
    EXPECT_THAT(file.is_open(), IsTrue()) << var << " names " << path << ", which is not in the runfiles";
    ++checked;
  }
  // Not a failure: an ordinary build sets none of these. Recorded so the log says which case ran.
  if (checked == 0) {
    GTEST_SKIP() << "no sanitizer options variable names a suppressions file in this configuration";
  }
}

}  // namespace
}  // namespace xff
