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

#include "xff/shard/shard.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

namespace xff::shard {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Optional;

// Matches a decoded shard by its load-bearing fields (identity + rendering width),
// leaving `total` / `tail` / `ext` to the dedicated matcher below when they matter.
auto ShardIs(Scheme scheme, std::string_view stem, std::int64_t index) {
  return Optional(AllOf(
      Field("scheme", &Match::scheme, Eq(scheme)), Field("stem", &Match::stem, Eq(stem)),
      Field("index", &Match::index, Eq(index))));
}

struct ShardMatcherTest : ::testing::Test {
  Matcher matcher = *Matcher::Make();
};

TEST_F(ShardMatcherTest, MakeRejectsATailRegexWithoutExactlyOneGroup) {
  EXPECT_THAT(
      Matcher::Make(TailSpec{.pattern = R"(\.[0-9a-f]+)"}),
      StatusIs(absl::StatusCode::kInvalidArgument));  // zero groups
  EXPECT_THAT(
      Matcher::Make(TailSpec{.pattern = R"(\.([0-9]+)-([0-9]+))"}),
      StatusIs(absl::StatusCode::kInvalidArgument));  // two groups
  EXPECT_THAT(
      Matcher::Make(TailSpec{.pattern = R"(\.((()))"}),
      StatusIs(absl::StatusCode::kInvalidArgument));  // not a valid regex
}

TEST_F(ShardMatcherTest, MakeWithADisabledTailStillBuilds) {
  EXPECT_THAT(Matcher::Make(TailSpec{.enabled = false}).status(), IsOk());
  EXPECT_THAT(Matcher::Make(TailSpec{.pattern = ""}).status(), IsOk());
}

TEST_F(ShardMatcherTest, PlainFileIsNotAShard) {
  EXPECT_THAT(matcher.Decode("README.md"), Eq(std::nullopt));
  EXPECT_THAT(matcher.Decode("archive.tar.gz"), Eq(std::nullopt));
  EXPECT_THAT(matcher.Decode("notes"), Eq(std::nullopt));
}

// ---- kOf ----

TEST_F(ShardMatcherTest, OfDecodesStemIndexTotalAndWidth) {
  const std::optional<Match> match = matcher.Decode("data-00000-of-00010");
  EXPECT_THAT(match, ShardIs(Scheme::kOf, "data", 0));
  EXPECT_THAT(match, Optional(Field("total", &Match::total, Optional(Eq(10)))));
  EXPECT_THAT(match, Optional(Field("width", &Match::width, Eq(5))));
  EXPECT_THAT(match, Optional(Field("tail", &Match::tail, IsEmpty())));
  EXPECT_THAT(match, Optional(Field("ext", &Match::ext, IsEmpty())));
}

TEST_F(ShardMatcherTest, OfStemIsGreedySoTheLastOfBinds) {
  EXPECT_THAT(matcher.Decode("a-1-of-2-3-of-9"), ShardIs(Scheme::kOf, "a-1-of-2", 3));
}

TEST_F(ShardMatcherTest, OfPreservesAnExtensionSeparatelyFromTheStem) {
  const std::optional<Match> match = matcher.Decode("part-00003-of-00042.tfrecord");
  EXPECT_THAT(match, ShardIs(Scheme::kOf, "part", 3));
  EXPECT_THAT(match, Optional(Field("ext", &Match::ext, Eq(".tfrecord"))));
  EXPECT_THAT(match, Optional(Field("tail", &Match::tail, IsEmpty())));
}

TEST_F(ShardMatcherTest, CustomPatternDecodesNamedGroups) {
  const Matcher custom =
      *Matcher::Make({}, std::vector<std::string>{R"((?P<stem>.*)_part(?P<index>\d+)of(?P<total>\d+))"});
  const std::optional<Match> match = custom.Decode("data_part02of05");
  EXPECT_THAT(match, ShardIs(Scheme::kCustom, "data", 2));
  EXPECT_THAT(match, Optional(Field("total", &Match::total, Optional(Eq(5)))));
  EXPECT_THAT(match, Optional(Field("width", &Match::width, Eq(2))));
  // Wildcard masks the index span in place (total kept verbatim).
  EXPECT_THAT(
      match, Optional(Field("wildcard", &Match::wildcard, Eq(absl::StrCat("data_part", std::string(2, '?'), "of05")))));
}

TEST_F(ShardMatcherTest, CustomPatternDupGroupIsExcludedFromIdentity) {
  const Matcher custom =
      *Matcher::Make({}, std::vector<std::string>{R"((?P<stem>.*)\.(?P<index>\d+)\.(?P<dup>[a-f0-9]+))"});
  const std::optional<Match> match = custom.Decode("blob.003.deadbeef");
  EXPECT_THAT(match, ShardIs(Scheme::kCustom, "blob", 3));
  EXPECT_THAT(match, Optional(Field("tail", &Match::tail, Eq("deadbeef"))));
}

TEST_F(ShardMatcherTest, CustomPatternWinsOverBuiltins) {
  // `data-00000-of-00003` is a built-in kOf name, but a custom pattern is tried first.
  const Matcher custom =
      *Matcher::Make({}, std::vector<std::string>{R"((?P<stem>.*)-(?P<index>\d+)-of-(?P<total>\d+))"});
  EXPECT_THAT(custom.Decode("data-00000-of-00003"), ShardIs(Scheme::kCustom, "data", 0));
}

TEST_F(ShardMatcherTest, CustomPatternRequiresStemAndIndexGroups) {
  EXPECT_THAT(
      Matcher::Make({}, std::vector<std::string>{R"((?P<stem>.*)-(\d+))"}).status(),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("index")));
}

TEST_F(ShardMatcherTest, CustomPatternInvalidRegexIsAnError) {
  EXPECT_THAT(
      Matcher::Make({}, std::vector<std::string>{"(?P<stem>.*)(?P<index>["}).status(),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("--shard-pattern")));
}

TEST_F(ShardMatcherTest, WildcardMasksTheIndexKeepingTotalExtDroppingTail) {
  // kOf: index -> `?` * width, total padding preserved, extension kept, tail dropped. The masked
  // names are built with StrCat so the source carries no `??-` (which C++ reads as a trigraph).
  const std::string mask5 = std::string(5, '?');
  EXPECT_THAT(
      matcher.Decode("part-00003-of-00042.tfrecord"),
      Optional(Field("wildcard", &Match::wildcard, Eq(absl::StrCat("part-", mask5, "-of-00042.tfrecord")))));
  EXPECT_THAT(
      matcher.Decode("data-00000-of-00010.deadbeefdeadbeef.tfrecord"),
      Optional(Field("wildcard", &Match::wildcard, Eq(absl::StrCat("data-", mask5, "-of-00010.tfrecord")))));
  // Suffix schemes: the separator is reused and the digits masked (these carry no trigraph).
  EXPECT_THAT(matcher.Decode("arc.001"), Optional(Field("wildcard", &Match::wildcard, Eq("arc.???"))));
  EXPECT_THAT(matcher.Decode("vol_07"), Optional(Field("wildcard", &Match::wildcard, Eq("vol_??"))));
}

TEST_F(ShardMatcherTest, OfCapturesTheHexTailAndExcludesItFromExt) {
  const std::optional<Match> match = matcher.Decode("data-00000-of-00010.a1b2c3d4e5f6a1b2");
  EXPECT_THAT(match, ShardIs(Scheme::kOf, "data", 0));
  EXPECT_THAT(match, Optional(Field("tail", &Match::tail, Eq("a1b2c3d4e5f6a1b2"))));
  EXPECT_THAT(match, Optional(Field("ext", &Match::ext, IsEmpty())));
}

TEST_F(ShardMatcherTest, OfKeepsTailBeforeExtensionSeparate) {
  const std::optional<Match> match = matcher.Decode("data-00000-of-00010.deadbeefdeadbeef.tfrecord");
  EXPECT_THAT(match, Optional(Field("tail", &Match::tail, Eq("deadbeefdeadbeef"))));
  EXPECT_THAT(match, Optional(Field("ext", &Match::ext, Eq(".tfrecord"))));
}

TEST_F(ShardMatcherTest, OfDoesNotMistakeAShortExtensionForATail) {
  // `.parquet` is 7 non-hex chars: not a tail, so it stays the extension.
  const std::optional<Match> match = matcher.Decode("data-00000-of-00010.parquet");
  EXPECT_THAT(match, Optional(Field("tail", &Match::tail, IsEmpty())));
  EXPECT_THAT(match, Optional(Field("ext", &Match::ext, Eq(".parquet"))));
}

TEST_F(ShardMatcherTest, DisabledTailLeavesTheTailTextInExt) {
  const Matcher no_tail = *Matcher::Make(TailSpec{.enabled = false});
  const std::optional<Match> match = no_tail.Decode("data-00000-of-00010.a1b2c3d4e5f6a1b2");
  EXPECT_THAT(match, Optional(Field("tail", &Match::tail, IsEmpty())));
  EXPECT_THAT(match, Optional(Field("ext", &Match::ext, Eq(".a1b2c3d4e5f6a1b2"))));
}

TEST_F(ShardMatcherTest, CustomTailRegexCarriesItsOwnSeparator) {
  const Matcher custom = *Matcher::Make(TailSpec{.pattern = R"(_gen-([0-9]+))"});
  const std::optional<Match> match = custom.Decode("data-00000-of-00010_gen-7.bin");
  EXPECT_THAT(match, Optional(Field("tail", &Match::tail, Eq("7"))));
  EXPECT_THAT(match, Optional(Field("ext", &Match::ext, Eq(".bin"))));
}

// ---- kDotNum / kUnderscore ----

TEST_F(ShardMatcherTest, DotNumDecodesA7ZipStyleVolume) {
  const std::optional<Match> match = matcher.Decode("backup.tar.001");
  EXPECT_THAT(match, ShardIs(Scheme::kDotNum, "backup.tar", 1));
  EXPECT_THAT(match, Optional(Field("width", &Match::width, Eq(3))));
  EXPECT_THAT(match, Optional(Field("total", &Match::total, Eq(std::nullopt))));
}

TEST_F(ShardMatcherTest, UnderscoreDecodesANumericUnderscoreSuffix) {
  EXPECT_THAT(matcher.Decode("chunk_007"), ShardIs(Scheme::kUnderscore, "chunk", 7));
  EXPECT_THAT(matcher.Decode("a_b_042"), ShardIs(Scheme::kUnderscore, "a_b", 42));
}

TEST_F(ShardMatcherTest, SuffixSchemesRequireAnAllDigitTail) {
  EXPECT_THAT(matcher.Decode("photo.jpeg"), Eq(std::nullopt));  // .jpeg is not digits
  EXPECT_THAT(matcher.Decode("draft_final"), Eq(std::nullopt));
  EXPECT_THAT(matcher.Decode("empty."), Eq(std::nullopt));
  EXPECT_THAT(matcher.Decode("empty_"), Eq(std::nullopt));
}

TEST_F(ShardMatcherTest, PathologicalNumericSuffixOverflowIsNotARealShardIndex) {
  EXPECT_THAT(matcher.Decode("part.999999999999999999999999999999"), ShardIs(Scheme::kDotNum, "part", 0));
}

TEST_F(ShardMatcherTest, OptionalCustomIndexMustParticipateInTheMatch) {
  const Matcher custom = *Matcher::Make({}, std::vector<std::string>{R"((?P<stem>item)(?:-(?P<index>\d+))?)"});
  EXPECT_THAT(custom.Decode("item"), Eq(std::nullopt));
  EXPECT_THAT(custom.Decode("item-12"), ShardIs(Scheme::kCustom, "item", 12));
}

TEST_F(ShardMatcherTest, SchemeNameMatchesTheFlagSpelling) {
  EXPECT_THAT(SchemeName(Scheme::kOf), Eq("of"));
  EXPECT_THAT(SchemeName(Scheme::kDotNum), Eq("dotnum"));
  EXPECT_THAT(SchemeName(Scheme::kUnderscore), Eq("underscore"));
  EXPECT_THAT(SchemeName(Scheme::kCustom), Eq("custom"));
  EXPECT_THAT(SchemeName(static_cast<Scheme>(255)), Eq("unknown"));
}

}  // namespace
}  // namespace xff::shard
