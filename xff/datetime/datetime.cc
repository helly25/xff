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

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "mbo/container/limited_map.h"

namespace xff::datetime {

std::optional<absl::Time> ParseTimeString(std::string_view text, absl::Time now, absl::TimeZone tz) {
  if (!text.empty() && text.front() == '@') {  // @epoch-seconds
    std::int64_t seconds = 0;
    if (!absl::SimpleAtoi(text.substr(1), &seconds)) {
      return std::nullopt;
    }
    return absl::FromUnixSeconds(seconds);
  }
  for (const std::string_view format : {"%Y-%m-%dT%H:%M:%S", "%Y-%m-%d %H:%M:%S", "%Y-%m-%d"}) {
    absl::Time time;
    std::string error;
    if (absl::ParseTime(format, text, tz, &time, &error)) {
      return time;
    }
  }
  const std::string lowered = absl::AsciiStrToLower(text);
  if (lowered == "now") {
    return now;
  }
  // Relative: "[+|-]N unit[s] [ago]".
  const std::vector<std::string_view> parts = absl::StrSplit(lowered, ' ', absl::SkipEmpty());
  if (parts.size() != 2 && parts.size() != 3) {
    return std::nullopt;
  }
  std::int64_t count = 0;
  if (!absl::SimpleAtoi(parts[0], &count)) {
    return std::nullopt;
  }
  if (parts.size() == 3) {
    if (parts[2] != "ago") {
      return std::nullopt;
    }
    if (count > 0) {
      count = -count;  // "ago" selects the past
    }
  }
  std::string_view unit = parts[1];
  if (unit.size() > 1 && unit.back() == 's') {
    unit.remove_suffix(1);  // accept the plural form
  }
  if (unit == "second" || unit == "sec") {
    return now + count * absl::Seconds(1);
  }
  if (unit == "minute" || unit == "min") {
    return now + count * absl::Minutes(1);
  }
  if (unit == "hour" || unit == "hr") {
    return now + count * absl::Hours(1);
  }
  if (unit == "day") {
    return now + count * absl::Hours(24);
  }
  if (unit == "week") {
    return now + count * absl::Hours(24 * 7);
  }
  if (unit == "month" || unit == "year") {
    const absl::TimeZone zone = tz;
    const absl::CivilSecond base = absl::ToCivilSecond(now, zone);
    if (unit == "year") {
      return absl::FromCivil(
          absl::CivilSecond(base.year() + count, base.month(), base.day(), base.hour(), base.minute(), base.second()),
          zone);
    }
    const absl::CivilMonth shifted = absl::CivilMonth(base.year(), base.month()) + count;
    return absl::FromCivil(
        absl::CivilSecond(shifted.year(), shifted.month(), base.day(), base.hour(), base.minute(), base.second()),
        zone);
  }
  return std::nullopt;
}

absl::Time StartOfDay(absl::Time t, absl::TimeZone tz) {
  return absl::FromCivil(absl::ToCivilDay(t, tz), tz);  // midnight of t's civil day in tz
}

bool ParseTimeZone(std::string_view spec, absl::TimeZone* out) {
  const std::string lowered = absl::AsciiStrToLower(spec);
  if (lowered.empty() || lowered == "local") {
    *out = absl::LocalTimeZone();
    return true;
  }
  if (lowered == "utc" || lowered == "z" || lowered == "zulu") {
    *out = absl::UTCTimeZone();
    return true;
  }
  return absl::LoadTimeZone(std::string(spec), out);  // IANA name (case-sensitive); false when unknown
}

// Preset time formats; any other spec is used verbatim as an absl::FormatTime
// pattern. Keyed alphabetically (constexpr dispatch, like the engine's tables).
// Only genuinely-conformant forms carry a standard's name; "space" is the readable
// default and claims none. epoch/zulu/zulu-dense are handled in FormatTime (they
// are numeric or force UTC), so they are not table entries.
constexpr std::string_view kIso8601 = "%Y-%m-%dT%H:%M:%S%z";  // ISO-8601 extended, shared by "iso"/"iso8601"
constexpr std::string_view kSpace = "%Y-%m-%d %H:%M:%S %z";   // readable default, shared by "space"/"human"

constexpr auto kNamedFormats = mbo::container::MakeLimitedMap(
    std::pair<std::string_view, std::string_view>{"asctime", "%a %b %e %H:%M:%S %Y"},   // asctime(3); find default %t
    std::pair<std::string_view, std::string_view>{"human", kSpace},                     // alias of space
    std::pair<std::string_view, std::string_view>{"iso", kIso8601},                     // shorthand for iso8601
    std::pair<std::string_view, std::string_view>{"iso8601", kIso8601},                 // ISO-8601 extended (T)
    std::pair<std::string_view, std::string_view>{"iso8601-basic", "%Y%m%dT%H%M%S%z"},  // ISO-8601 basic (compact)
    std::pair<std::string_view, std::string_view>{"iso8601-full", "%Y-%m-%dT%H:%M:%E9S%z"},  // ISO-8601 + sub-second
    std::pair<std::string_view, std::string_view>{"rfc3339", "%Y-%m-%dT%H:%M:%S%Ez"},        // RFC 3339 (colon offset)
    std::pair<std::string_view, std::string_view>{"space", kSpace});  // readable default (primary)

std::string FormatTime(absl::Time time, std::string_view spec, absl::TimeZone tz) {
  if (spec == "epoch") {
    return std::to_string(absl::ToUnixSeconds(time));
  }
  if (spec == "zulu") {  // UTC with a 'Z' designator (extended), regardless of `tz`
    return absl::FormatTime("%Y-%m-%dT%H:%M:%SZ", time, absl::UTCTimeZone());
  }
  if (spec == "zulu-dense") {  // UTC 'Z', no separators (compact)
    return absl::FormatTime("%Y%m%dT%H%M%SZ", time, absl::UTCTimeZone());
  }
  std::string_view pattern = spec.empty() ? std::string_view("space") : spec;
  if (const auto it = kNamedFormats.find(pattern); it != kNamedFormats.end()) {
    pattern = it->second;  // a preset name resolves to its pattern; anything else is used verbatim
  }
  return absl::FormatTime(pattern, time, tz);
}

}  // namespace xff::datetime
