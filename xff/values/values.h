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

#ifndef XFF_VALUES_VALUES_H_
#define XFF_VALUES_VALUES_H_

#include <cstdint>
#include <optional>
#include <string_view>

// Shared parsing of CLI option values into one permissive, uniform vocabulary, so
// every boolean-ish flag accepts the same spellings. A boolean flag accepts
// yes / true / on / 1 (true) and no / false / off / 0 (false); a tri-state flag
// additionally accepts auto, plus the color-family idiom always / never. All
// case-insensitive.
namespace xff::values {

// A tri-state option value: forced on, forced off, or decide automatically
// (typically: on iff stdout is a terminal).
enum class Tristate : std::uint8_t { kOff, kOn, kAuto };

// Parses a boolean option value. Accepts (case-insensitive) yes / true / on / 1 as
// true and no / false / off / 0 as false; returns nullopt for anything else.
//
// on / off are in the vocabulary because a switch-shaped flag reads best that way
// (--gitignore=on), and because leaving them out made the help lie: several flags
// documented them as synonyms while the parser rejected the value and silently kept
// the default.
[[nodiscard]] std::optional<bool> ParseBool(std::string_view value);

// Parses a tri-state option value: auto as kAuto, and everything ParseBool accepts
// (plus always -> on, never -> off) mapped to kOn / kOff; nullopt otherwise.
[[nodiscard]] std::optional<Tristate> ParseTristate(std::string_view value);

// Parses an explicit byte-unit suffix. SI suffixes (`B`, `kB`, `MB`, ... `EB`) use powers of 1000;
// IEC suffixes (`KiB`, `MiB`, ... `EiB`) use powers of 1024. Input is case-insensitive, while help
// and output use the canonical spellings above. A unit must include the trailing B so it cannot be
// confused with historical find units such as `M` (binary) or row-count multipliers.
[[nodiscard]] std::optional<std::uint64_t> ParseByteUnit(std::string_view unit);

// Parses an unsigned integer followed by ParseByteUnit's explicit suffix, rejecting multiplication
// overflow. A bare integer is deliberately not accepted: callers give those values domain-specific
// meanings (bytes, blocks, or rows).
[[nodiscard]] std::optional<std::uint64_t> ParseByteSize(std::string_view value);

}  // namespace xff::values

#endif  // XFF_VALUES_VALUES_H_
