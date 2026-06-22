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
//   [+|-]N unit[s] [ago]  relative to `now`; unit is one of
//                         sec(ond)/min(ute)/hour(hr)/day/week/month/year.
//
// In the relative form a leading '-' OR a trailing "ago" selects the past,
// while '+' or neither selects the future, so "-3 days" equals "3 days ago".
// That is the logical reading; it diverges from GNU get_date only on the
// redundant "-N ... ago" (which get_date flips back to the future). Matching is
// case-insensitive and the plural unit form is accepted. Returns nullopt when
// nothing matches (callers treat that as "no match"); get_date's wider grammar
// ("next Tuesday", combined terms) is intentionally not supported.
std::optional<absl::Time> ParseTimeString(std::string_view text, absl::Time now);

}  // namespace xff::datetime

#endif  // XFF_DATETIME_DATETIME_H_
