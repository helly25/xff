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
#include <string>

#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::datetime {
namespace {

using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Optional;

struct DateTimeTest : ::testing::Test {};

// A fixed reference instant for the relative forms. Its exact value is
// irrelevant for the duration-based units (they offset by a fixed Duration);
// month/year use the local zone consistently on both sides of the assertion.
absl::Time Now() {
  return absl::FromCivil(absl::CivilSecond(2'021, 3, 15, 12, 30, 0), absl::LocalTimeZone());
}

TEST_F(DateTimeTest, EpochSeconds) {
  EXPECT_THAT(ParseTimeString("@1600000000", Now()), Optional(Eq(absl::FromUnixSeconds(1'600'000'000))));
  EXPECT_THAT(ParseTimeString("@0", Now()), Optional(Eq(absl::FromUnixSeconds(0))));
  EXPECT_THAT(ParseTimeString("@", Now()), Eq(std::nullopt));
  EXPECT_THAT(ParseTimeString("@12x", Now()), Eq(std::nullopt));
}

TEST_F(DateTimeTest, IsoAbsoluteFormsInLocalZone) {
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
  EXPECT_THAT(ParseTimeString("2020/01/02", Now()), Eq(std::nullopt));
  EXPECT_THAT(ParseTimeString("not-a-date", Now()), Eq(std::nullopt));
}

TEST_F(DateTimeTest, NowKeywordIsCaseInsensitive) {
  EXPECT_THAT(ParseTimeString("now", Now()), Optional(Eq(Now())));
  EXPECT_THAT(ParseTimeString("NOW", Now()), Optional(Eq(Now())));
}

TEST_F(DateTimeTest, RelativeDurationUnits) {
  const absl::Time t = Now();
  EXPECT_THAT(ParseTimeString("30 seconds ago", t), Optional(Eq(t - absl::Seconds(30))));
  EXPECT_THAT(ParseTimeString("5 minutes ago", t), Optional(Eq(t - absl::Minutes(5))));
  EXPECT_THAT(ParseTimeString("2 hours ago", t), Optional(Eq(t - absl::Hours(2))));
  EXPECT_THAT(ParseTimeString("3 days ago", t), Optional(Eq(t - absl::Hours(72))));
  EXPECT_THAT(ParseTimeString("1 week ago", t), Optional(Eq(t - absl::Hours(24 * 7))));
}

TEST_F(DateTimeTest, RelativeCalendarUnits) {
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

TEST_F(DateTimeTest, SignAndAgoSelectDirection) {
  const absl::Time t = Now();
  // Past: leading '-', or trailing "ago", or both (the redundant case stays past).
  EXPECT_THAT(ParseTimeString("3 days ago", t), Optional(Eq(t - absl::Hours(72))));
  EXPECT_THAT(ParseTimeString("-3 days", t), Optional(Eq(t - absl::Hours(72))));
  EXPECT_THAT(ParseTimeString("-3 days ago", t), Optional(Eq(t - absl::Hours(72))));
  // Future: '+' or no sign, and no "ago".
  EXPECT_THAT(ParseTimeString("3 days", t), Optional(Eq(t + absl::Hours(72))));
  EXPECT_THAT(ParseTimeString("+3 days", t), Optional(Eq(t + absl::Hours(72))));
}

TEST_F(DateTimeTest, CompoundDurations) {
  const absl::TimeZone zone = absl::LocalTimeZone();
  const absl::Time t = Now();  // 2021-03-15 12:30:00 local
  // Multiple fixed-duration terms sum into one offset; the leading sign or "ago"
  // applies to the whole sum.
  EXPECT_THAT(ParseTimeString("3 weeks 3 hours", t), Optional(Eq(t + absl::Hours(24 * 7 * 3) + absl::Hours(3))));
  EXPECT_THAT(ParseTimeString("-3 weeks 3 hours", t), Optional(Eq(t - absl::Hours(24 * 7 * 3) - absl::Hours(3))));
  EXPECT_THAT(
      ParseTimeString("1 day 2 hours 30 minutes ago", t),
      Optional(Eq(t - absl::Hours(24) - absl::Hours(2) - absl::Minutes(30))));
  // Calendar and fixed-duration terms combine: shift the civil date, then offset.
  EXPECT_THAT(
      ParseTimeString("1 year 2 days ago", t),
      Optional(Eq(absl::FromCivil(absl::CivilSecond(2'020, 3, 15, 12, 30, 0), zone) - absl::Hours(48))));
  // A dangling count (an odd run of tokens) is rejected.
  EXPECT_THAT(ParseTimeString("3 weeks 3", t), Eq(std::nullopt));
}

TEST_F(DateTimeTest, PluralSingularAliasesAndCase) {
  const absl::Time t = Now();
  EXPECT_THAT(ParseTimeString("1 day ago", t), Optional(Eq(t - absl::Hours(24))));
  EXPECT_THAT(ParseTimeString("1 DAY AGO", t), Optional(Eq(t - absl::Hours(24))));
  EXPECT_THAT(ParseTimeString("90 sec ago", t), Optional(Eq(t - absl::Seconds(90))));
  EXPECT_THAT(ParseTimeString("90 secs ago", t), Optional(Eq(t - absl::Seconds(90))));
  EXPECT_THAT(ParseTimeString("2 hr ago", t), Optional(Eq(t - absl::Hours(2))));
}

TEST_F(DateTimeTest, UnparseableReturnsNullopt) {
  const absl::Time t = Now();
  EXPECT_THAT(ParseTimeString("", t), Eq(std::nullopt));
  EXPECT_THAT(ParseTimeString("yesterday", t), Eq(std::nullopt));
  EXPECT_THAT(ParseTimeString("next tuesday", t), Eq(std::nullopt));
  EXPECT_THAT(ParseTimeString("1 day 5 fortnights", t), Eq(std::nullopt));  // unknown unit in a later term
  EXPECT_THAT(ParseTimeString("5 fortnights", t), Eq(std::nullopt));        // unknown unit
  EXPECT_THAT(ParseTimeString("days ago", t), Eq(std::nullopt));            // missing count
}

TEST_F(DateTimeTest, FormatTimePresetsAndCustomPatterns) {
  const absl::TimeZone utc = absl::UTCTimeZone();
  const absl::Time t = absl::FromUnixSeconds(1'600'000'000);  // 2020-09-13 12:26:40 UTC (a Sunday)
  EXPECT_THAT(FormatTime(t, "epoch", utc), "1600000000");
  EXPECT_THAT(FormatTime(t, "iso8601", utc), "2020-09-13T12:26:40+0000");
  EXPECT_THAT(FormatTime(t, "iso", utc), "2020-09-13T12:26:40+0000");  // "iso" aliases iso8601
  EXPECT_THAT(FormatTime(t, "iso8601-basic", utc), "20200913T122640+0000");
  EXPECT_THAT(FormatTime(t, "iso8601-full", utc), "2020-09-13T12:26:40.000000000+0000");   // sub-second
  EXPECT_THAT(FormatTime(t, "rfc3339", utc), "2020-09-13T12:26:40+00:00");                 // colon offset
  EXPECT_THAT(FormatTime(t, "space", utc), "2020-09-13 12:26:40 +0000");                   // space before offset
  EXPECT_THAT(FormatTime(t, "human", utc), "2020-09-13 12:26:40 +0000");                   // alias of space
  EXPECT_THAT(FormatTime(t, "", utc), "2020-09-13 12:26:40 +0000");                        // empty -> space default
  EXPECT_THAT(FormatTime(t, "asctime", utc), "Sun Sep 13 12:26:40 2020");                  // find's default %t
  EXPECT_THAT(FormatTime(t, "zulu", utc), "2020-09-13T12:26:40Z");                         // UTC, Z
  EXPECT_THAT(FormatTime(t, "zulu", absl::FixedTimeZone(3'600)), "2020-09-13T12:26:40Z");  // zulu forces UTC
  EXPECT_THAT(FormatTime(t, "zulu-dense", utc), "20200913T122640Z");                       // Zulu, no separators
  EXPECT_THAT(FormatTime(t, "%Y/%m/%d", utc), "2020/09/13");                               // custom pattern
}

TEST_F(DateTimeTest, ParseTimeZoneAcceptsLocalUtcAndNamedZones) {
  absl::TimeZone zone;
  EXPECT_THAT(ParseTimeZone("", &zone), IsTrue());  // empty -> local
  EXPECT_THAT(zone, Eq(absl::LocalTimeZone()));
  EXPECT_THAT(ParseTimeZone("local", &zone), IsTrue());
  EXPECT_THAT(zone, Eq(absl::LocalTimeZone()));
  EXPECT_THAT(ParseTimeZone("UTC", &zone), IsTrue());
  EXPECT_THAT(zone, Eq(absl::UTCTimeZone()));
  EXPECT_THAT(ParseTimeZone("utc", &zone), IsTrue());  // special names are case-insensitive
  EXPECT_THAT(zone, Eq(absl::UTCTimeZone()));
  EXPECT_THAT(ParseTimeZone("zulu", &zone), IsTrue());
  EXPECT_THAT(zone, Eq(absl::UTCTimeZone()));
  // An IANA name resolves via the system zone database (delegated to LoadTimeZone).
  absl::TimeZone expected;
  ASSERT_THAT(absl::LoadTimeZone("America/New_York", &expected), IsTrue());
  absl::TimeZone got;
  ASSERT_THAT(ParseTimeZone("America/New_York", &got), IsTrue());
  EXPECT_THAT(got, Eq(expected));
  // An unknown zone fails (so the caller can report a usage error).
  EXPECT_THAT(ParseTimeZone("Not/AZone", &zone), IsFalse());
}

TEST_F(DateTimeTest, StartOfDayFloorsToZoneMidnight) {
  const absl::TimeZone utc = absl::UTCTimeZone();
  // A mid-afternoon instant collapses to 00:00:00 of the same calendar day.
  const absl::Time afternoon = absl::FromCivil(absl::CivilSecond(2'026, 6, 27, 14, 30, 45), utc);
  EXPECT_THAT(StartOfDay(afternoon, utc), Eq(absl::FromCivil(absl::CivilDay(2'026, 6, 27), utc)));
  // Exactly midnight is a fixed point.
  const absl::Time midnight = absl::FromCivil(absl::CivilDay(2'026, 6, 27), utc);
  EXPECT_THAT(StartOfDay(midnight, utc), Eq(midnight));
  // The day boundary is the zone's, not UTC's: 00:30 at +02:00 floors to that
  // zone's local midnight (two hours before the UTC day even begins).
  const absl::TimeZone plus2 = absl::FixedTimeZone(2 * 60 * 60);
  const absl::Time early = absl::FromCivil(absl::CivilSecond(2'026, 6, 27, 0, 30, 0), plus2);
  EXPECT_THAT(StartOfDay(early, plus2), Eq(absl::FromCivil(absl::CivilDay(2'026, 6, 27), plus2)));
}

TEST_F(DateTimeTest, TimeZoneArgInterpretsCalendarFormsButNotAbsoluteOnes) {
  const absl::TimeZone utc = absl::UTCTimeZone();
  const absl::TimeZone plus1 = absl::FixedTimeZone(3'600);  // UTC+1
  EXPECT_THAT(
      ParseTimeString("2020-01-02", Now(), utc),
      Optional(Eq(absl::FromCivil(absl::CivilSecond(2'020, 1, 2, 0, 0, 0), utc))));
  EXPECT_THAT(
      ParseTimeString("2020-01-02", Now(), plus1),
      Optional(Eq(absl::FromCivil(absl::CivilSecond(2'020, 1, 2, 0, 0, 0), plus1))));
  // Midnight in UTC is one hour after midnight in UTC+1 (same wall-clock date).
  EXPECT_THAT(
      ParseTimeString("2020-01-02", Now(), utc),
      Optional(Eq(*ParseTimeString("2020-01-02", Now(), plus1) + absl::Hours(1))));
  // @epoch is an absolute instant: the zone must not shift it.
  EXPECT_THAT(ParseTimeString("@1600000000", Now(), plus1), Optional(Eq(absl::FromUnixSeconds(1'600'000'000))));
}

}  // namespace
}  // namespace xff::datetime
