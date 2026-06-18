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

// Formats matched paths into output records. Stateless aside from the format
// selector; cheap to copy.
class Renderer {
 public:
  explicit Renderer(Format format) : format_(format) {}

  // Returns the output record for `path`, terminator included:
  //   kPlain -> "path\n", kNul -> "path\0", kJsonl -> {"path":"<escaped>"}\n.
  // For kJsonl the path is JSON-string-escaped (quote, backslash, control
  // characters); non-UTF-8 byte handling is refined later with --path-encoding.
  std::string Record(std::string_view path) const;

 private:
  Format format_;
};

}  // namespace xff::render

#endif  // XFF_RENDER_RENDER_H_
