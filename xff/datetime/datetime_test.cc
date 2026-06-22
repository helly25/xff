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

#include "xff/datetime/datetime.h"

#include <optional>

#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::datetime {
namespace {

using ::testing::Eq;
using ::testing::Optional;

// A fixed reference instant for the relative forms. Its exact value is
// irrelevant for the duration-based units (they offset by a fixed Duration);
// month/year use the local zone consistently on both sides of the assertion.
absl::Time Now() {
  return absl::FromCivil(absl::CivilSecond(2'021, 3, 15, 12, 30, 0), absl::LocalTimeZone());
}

TEST(DateTimeTest, EpochSeconds) {
  EXPECT_THAT(ParseTimeString("@1600000000", Now()), Optional(Eq(absl::FromUnixSeconds(1'600'000'000))));
  EXPECT_THAT(ParseTimeString("@0", Now()), Optional(Eq(absl::FromUnixSeconds(0))));
  EXPECT_EQ(ParseTimeString("@", Now()), std::nullopt);
  EXPECT_EQ(ParseTimeString("@12x", Now()), std::nullopt);
}

TEST(DateTimeTest, IsoAbsoluteFormsInLocalZone) {
  const absl::TimeZone zone = absl::LocalTimeZone();
  EXPECT_THAT(
      ParseTimeString("2020-01-02", Now()),
      Optional(Eq(absl::FromCivil(absl::CivilSecond(2'020, 1, 2, 0, 0, 0), zone))));
  EXPECT_THAT(
      ParseTimeString("2020-01-02 15:04:05", Now()),
      Optional(Eq(absl::FromCivil(absl::CivilSecond(2'020, 1, 2, 15, 4, 5), zone))));
  EXPECT_THAT(
      ParseTimeString("2020-01-02T15:04:05", Now()),
      Optional(Eq(absl::FromCivil(absl::CivilSecond(2'020, 1, 2, 15, 4, 5), zone))));
  EXPECT_EQ(ParseTimeString("2020/01/02", Now()), std::nullopt);
  EXPECT_EQ(ParseTimeString("not-a-date", Now()), std::nullopt);
}

TEST(DateTimeTest, NowKeywordIsCaseInsensitive) {
  EXPECT_THAT(ParseTimeString("now", Now()), Optional(Eq(Now())));
  EXPECT_THAT(ParseTimeString("NOW", Now()), Optional(Eq(Now())));
}

TEST(DateTimeTest, RelativeDurationUnits) {
  const absl::Time t = Now();
  EXPECT_THAT(ParseTimeString("30 seconds ago", t), Optional(Eq(t - absl::Seconds(30))));
  EXPECT_THAT(ParseTimeString("5 minutes ago", t), Optional(Eq(t - absl::Minutes(5))));
  EXPECT_THAT(ParseTimeString("2 hours ago", t), Optional(Eq(t - absl::Hours(2))));
  EXPECT_THAT(ParseTimeString("3 days ago", t), Optional(Eq(t - absl::Hours(72))));
  EXPECT_THAT(ParseTimeString("1 week ago", t), Optional(Eq(t - absl::Hours(24 * 7))));
}

TEST(DateTimeTest, RelativeCalendarUnits) {
  const absl::TimeZone zone = absl::LocalTimeZone();
  const absl::Time t = Now();  // 2021-03-15 12:30:00 local
  EXPECT_THAT(
      ParseTimeString("1 month ago", t),
      Optional(Eq(absl::FromCivil(absl::CivilSecond(2'021, 2, 15, 12, 30, 0), zone))));
  EXPECT_THAT(
      ParseTimeString("13 months ago", t),  // crosses the year boundary
      Optional(Eq(absl::FromCivil(absl::CivilSecond(2'020, 2, 15, 12, 30, 0), zone))));
  EXPECT_THAT(
      ParseTimeString("2 years ago", t),
      Optional(Eq(absl::FromCivil(absl::CivilSecond(2'019, 3, 15, 12, 30, 0), zone))));
}

TEST(DateTimeTest, SignAndAgoSelectDirection) {
  const absl::Time t = Now();
  // Past: leading '-', or trailing "ago", or both (the redundant case stays past).
  EXPECT_THAT(ParseTimeString("3 days ago", t), Optional(Eq(t - absl::Hours(72))));
  EXPECT_THAT(ParseTimeString("-3 days", t), Optional(Eq(t - absl::Hours(72))));
  EXPECT_THAT(ParseTimeString("-3 days ago", t), Optional(Eq(t - absl::Hours(72))));
  // Future: '+' or no sign, and no "ago".
  EXPECT_THAT(ParseTimeString("3 days", t), Optional(Eq(t + absl::Hours(72))));
  EXPECT_THAT(ParseTimeString("+3 days", t), Optional(Eq(t + absl::Hours(72))));
}

TEST(DateTimeTest, PluralSingularAliasesAndCase) {
  const absl::Time t = Now();
  EXPECT_THAT(ParseTimeString("1 day ago", t), Optional(Eq(t - absl::Hours(24))));
  EXPECT_THAT(ParseTimeString("1 DAY AGO", t), Optional(Eq(t - absl::Hours(24))));
  EXPECT_THAT(ParseTimeString("90 sec ago", t), Optional(Eq(t - absl::Seconds(90))));
  EXPECT_THAT(ParseTimeString("90 secs ago", t), Optional(Eq(t - absl::Seconds(90))));
  EXPECT_THAT(ParseTimeString("2 hr ago", t), Optional(Eq(t - absl::Hours(2))));
}

TEST(DateTimeTest, UnparseableReturnsNullopt) {
  const absl::Time t = Now();
  EXPECT_EQ(ParseTimeString("", t), std::nullopt);
  EXPECT_EQ(ParseTimeString("yesterday", t), std::nullopt);
  EXPECT_EQ(ParseTimeString("next tuesday", t), std::nullopt);
  EXPECT_EQ(ParseTimeString("2 days 3 hours ago", t), std::nullopt);  // combined terms
  EXPECT_EQ(ParseTimeString("5 fortnights", t), std::nullopt);        // unknown unit
  EXPECT_EQ(ParseTimeString("days ago", t), std::nullopt);            // missing count
}

}  // namespace
}  // namespace xff::datetime
