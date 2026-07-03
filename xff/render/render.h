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

#ifndef XFF_RENDER_RENDER_H_
#define XFF_RENDER_RENDER_H_

#include <string>
#include <string_view>

namespace xff::render {

// Output record format for matched paths. kPlain/kNul mirror find's
// -print/-print0; kJsonl is xff's modern one-object-per-line stream.
enum class Format { kPlain, kNul, kJsonl };

// How path bytes are emitted (xff `--path-encoding`). kRaw writes the bytes
// verbatim (find-compatible default). kEscape C-escapes the backslash and control
// characters (`\n`, `\t`, `\r`, else `\xNN`) so a newline or control byte in a
// filename cannot corrupt the line-oriented kPlain stream. It applies only to
// kPlain: kNul stays raw by design (the NUL is the separator) and kJsonl always
// JSON-escapes regardless.
enum class PathEncoding { kRaw, kEscape };

// Formats matched paths into output records. Stateless aside from the format +
// encoding selectors; cheap to copy.
class Renderer {
 public:
  explicit Renderer(Format format, PathEncoding encoding = PathEncoding::kRaw) : format_(format), encoding_(encoding) {}

  // Returns the output record for `path`, terminator included:
  //   kPlain -> "path\n" (path C-escaped when encoding is kEscape),
  //   kNul   -> "path\0" (always raw), kJsonl -> {"path":"<JSON-escaped>"}\n.
  // `color` is an ANSI SGR parameter (e.g. "1;34"); when non-empty it wraps the
  // kPlain path in `\e[<color>m...\e[0m` (--color). Ignored for kNul (the NUL is the
  // separator) and kJsonl (a machine format), which stay uncolored.
  std::string Record(std::string_view path, std::string_view color = {}) const;

 private:
  Format format_;
  PathEncoding encoding_;
};

}  // namespace xff::render

#endif  // XFF_RENDER_RENDER_H_
