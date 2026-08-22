// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
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

#ifndef XFF_CLI_WRAP_H_
#define XFF_CLI_WRAP_H_

#include <cstddef>
#include <string>
#include <string_view>

// Indent-aware word wrapping for the plain help backend (#153 / #164). The wrap
// column comes from --width (see help_width.h); PlainTextBackend routes its flowing
// text - prose, bullets, an entry summary / detail, a see-also note - through this.
namespace xff::cli {

// Word-wraps `text` into `width` columns, emitting the first line behind
// `first_indent` and each continuation line behind `cont_indent`. Words split on
// runs of ASCII whitespace (prose arrives already joined onto one line); a word
// wider than the remaining budget still takes its own line rather than being split
// mid-word. The budget is per line - `width` minus that line's indent - so wrapping
// is indent-aware. A `width` of 0 disables wrapping: the text becomes one
// `first_indent`-prefixed line, emitted even when empty (so it byte-reproduces the
// pre-wrap "indent + text + newline" output). At a positive width, empty `text`
// yields the empty string. Every emitted line ends in '\n'.
[[nodiscard]] std::string WrapText(
    std::string_view text,
    std::size_t width,
    std::string_view first_indent,
    std::string_view cont_indent);

}  // namespace xff::cli

#endif  // XFF_CLI_WRAP_H_
