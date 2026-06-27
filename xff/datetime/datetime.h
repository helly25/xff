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

#ifndef XFF_DATETIME_DATETIME_H_
#define XFF_DATETIME_DATETIME_H_

#include <optional>
#include <string>
#include <string_view>

#include "absl/time/time.h"

namespace xff::datetime {

// Parses a human time string into an absolute instant, backing find's -newerXt
// (and reusable wherever a time argument is accepted). Recognized forms:
//
//   @<seconds>            Unix epoch seconds.
//   YYYY-MM-DD            ISO-8601 date in the local zone (time 00:00:00).
//   YYYY-MM-DD HH:MM:SS   ISO-8601 date-time in the local zone (space or 'T').
//   now                   the supplied reference instant `now`.
//   [+|-]N unit[s] [N unit[s] ...] [ago]
//                         one or more count+unit terms summed into one offset
//                         from `now`; unit is one of
//                         sec(ond)/min(ute)/hour(hr)/day/week/month/year, e.g.
//                         "3 weeks 3 hours" or "1 day 2 hours 30 minutes ago".
//
// In the relative form a leading '-' on the first count OR a trailing "ago"
// selects the past for the whole sum, while '+' or neither selects the future,
// so "-3 days" equals "3 days ago" and "-3 weeks 3 hours" is (3 weeks + 3 hours)
// in the past. That is the logical reading; it diverges from GNU get_date only on
// the redundant "-N ... ago" (still the past here). Matching is
// case-insensitive and the plural unit form is accepted. Returns nullopt when
// nothing matches (callers treat that as "no match"); get_date's wider grammar
// ("next Tuesday", combined terms) is intentionally not supported. The calendar
// forms (YYYY-MM-DD[ HH:MM:SS]) and the month/year shifts are interpreted in
// `tz` (default the local zone; --timezone overrides it); @epoch and the
// second..week shifts are absolute instants and ignore `tz`.
std::optional<absl::Time> ParseTimeString(
    std::string_view text,
    absl::Time now,
    absl::TimeZone tz = absl::LocalTimeZone());

// The start of `t`'s calendar day (local midnight, 00:00:00) in `tz`, backing
// find's -daystart: the reference instant the day/minute age tests (-mtime,
// -mmin, -atime, ...) measure from when -daystart is given, instead of the run's
// start time. `tz` defaults to the local zone (--timezone overrides it, matching
// the time tests).
absl::Time StartOfDay(absl::Time t, absl::TimeZone tz = absl::LocalTimeZone());

// Resolves a --timezone spec to an absl::TimeZone, writing it to *out and
// returning true on success. Accepts "" or "local" (the host's local zone),
// "utc"/"z"/"zulu" (UTC, case-insensitive), a fixed UTC offset "+HH", "+HH:MM",
// or "+HHMM" and the '-' forms ("+05:30", "-0800", "+01"), and any IANA zone name
// ("America/New_York", "Europe/London") loaded from the system zone database.
// Returns false (leaving *out unchanged) for an unknown name or a malformed offset,
// so the caller can report a usage error rather than silently falling back.
bool ParseTimeZone(std::string_view spec, absl::TimeZone* out);

// Formats `time`, the inverse of ParseTimeString. `spec` is a preset name or any
// absl::FormatTime() pattern. Presets (only conformant forms claim a standard):
//   (empty)/"space"/"human"  readable default, no standard:  2026-06-22 14:30:00 +0100
//   "iso" / "iso8601"        ISO-8601 extended ('T'):        2026-06-22T14:30:00+0100
//   "iso8601-basic"          ISO-8601 basic (compact):       20260622T143000+0100
//   "iso8601-full"           ISO-8601 + sub-second:          2026-06-22T14:30:00.000000000+0100
//   "rfc3339"                RFC 3339 (colon offset):        2026-06-22T14:30:00+01:00
//   "asctime"                asctime(3); find's default %t:  Mon Jun 22 14:30:00 2026
//   "zulu"                   military Zulu, always UTC:       2026-06-22T14:30:00Z
//   "zulu-dense"             Zulu, no separators:            20260622T143000Z
//   "epoch"                  Unix seconds:                   1781612345
//   <pattern>           used verbatim as an absl::FormatTime() pattern
// `tz` defaults to the local zone; tests pass a fixed zone for determinism.
std::string FormatTime(absl::Time time, std::string_view spec = {}, absl::TimeZone tz = absl::LocalTimeZone());

}  // namespace xff::datetime

#endif  // XFF_DATETIME_DATETIME_H_
