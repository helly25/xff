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

#include "xff/values/values.h"

#include <optional>
#include <string>
#include <string_view>

#include "absl/strings/ascii.h"

namespace xff::values {

std::optional<bool> ParseBool(std::string_view value) {
  const std::string lower = absl::AsciiStrToLower(value);
  if (lower == "yes" || lower == "true" || lower == "1") {
    return true;
  }
  if (lower == "no" || lower == "false" || lower == "0") {
    return false;
  }
  return std::nullopt;
}

std::optional<Tristate> ParseTristate(std::string_view value) {
  const std::string lower = absl::AsciiStrToLower(value);
  if (lower == "auto") {
    return Tristate::kAuto;
  }
  if (lower == "always") {
    return Tristate::kOn;
  }
  if (lower == "never") {
    return Tristate::kOff;
  }
  if (const std::optional<bool> parsed = ParseBool(lower); parsed.has_value()) {
    return *parsed ? Tristate::kOn : Tristate::kOff;
  }
  return std::nullopt;
}

}  // namespace xff::values
