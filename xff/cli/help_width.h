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

#ifndef XFF_CLI_HELP_WIDTH_H_
#define XFF_CLI_HELP_WIDTH_H_

#include <cstddef>
#include <optional>
#include <string_view>

#include "absl/status/statusor.h"

// Resolves the wrap column for plain `--help` / topic text from the `--width` flag
// and the terminal (#153 / #164). The width then drives PlainTextBackend's word-wrap
// (see wrap.h / plain_backend.h); a resolved 0 means "do not wrap".
namespace xff::cli {

// The wrap column used when the terminal width is unknown (auto with no detectable
// terminal). The conventional fallback (fold / fmt / GNU --help), used as a literal
// budget - a wrapped line may fill all 80 columns.
inline constexpr std::size_t kFallbackHelpWidth = 80;

// Resolves the plain-help wrap width from the `--width` flag and the terminal.
//   `flag`          the raw --width value, or nullopt when the flag is absent.
//   `detected_cols` the known terminal width, or 0 when unknown (DetectTerminalWidth).
// Values (case-insensitive): "auto" or absent -> `detected_cols` if > 0, else
// kFallbackHelpWidth; "none" or "0" -> 0 (no wrapping); a positive integer -> that
// many columns. Any other value is an error. A returned 0 means "do not wrap". This
// is the pure, tested seam (DetectTerminalWidth reads the environment for it).
[[nodiscard]] absl::StatusOr<std::size_t> ResolveHelpWidth(
    std::optional<std::string_view> flag,
    std::size_t detected_cols);

// The terminal width for stdout: $COLUMNS when it is a positive integer, else the
// tty's column count via ioctl when stdout is a tty, else 0 (unknown). Reads the
// environment and stdout, so it is the impure companion to ResolveHelpWidth.
[[nodiscard]] std::size_t DetectTerminalWidth();

}  // namespace xff::cli

#endif  // XFF_CLI_HELP_WIDTH_H_
