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

std::optional<absl::Time> ParseTimeString(std::string_view text, absl::Time now) {
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
    if (absl::ParseTime(format, text, absl::LocalTimeZone(), &time, &error)) {
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
    const absl::TimeZone zone = absl::LocalTimeZone();
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

// The ctime(3)/asctime(3) layout, shared by the "find" and "asctime" presets.
constexpr std::string_view kAsctime = "%a %b %e %H:%M:%S %Y";

// Preset time formats; any other spec is used verbatim as an absl::FormatTime
// pattern. Keyed alphabetically (constexpr dispatch, like the engine's tables).
constexpr auto kNamedFormats = mbo::container::MakeLimitedMap(
    std::pair<std::string_view, std::string_view>{"asctime", kAsctime},                // asctime(3) layout
    std::pair<std::string_view, std::string_view>{"find", kAsctime},                   // find's %t (same layout)
    std::pair<std::string_view, std::string_view>{"iso", "%Y-%m-%dT%H:%M:%S%z"},       // ISO-8601, +0100 offset
    std::pair<std::string_view, std::string_view>{"rfc3339", "%Y-%m-%dT%H:%M:%S%Ez"},  // RFC 3339, +01:00 offset
    std::pair<std::string_view, std::string_view>{"space", "%Y-%m-%d %H:%M:%S%z"});    // ISO + space (default)

std::string FormatTime(absl::Time time, std::string_view spec, absl::TimeZone tz) {
  if (spec == "epoch") {
    return std::to_string(absl::ToUnixSeconds(time));
  }
  std::string_view pattern = spec.empty() ? std::string_view("space") : spec;
  if (const auto it = kNamedFormats.find(pattern); it != kNamedFormats.end()) {
    pattern = it->second;  // a preset name resolves to its pattern; anything else is used verbatim
  }
  return absl::FormatTime(pattern, time, tz);
}

}  // namespace xff::datetime
