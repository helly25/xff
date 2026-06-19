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

#ifndef XFF_FIELDS_FIELDS_H_
#define XFF_FIELDS_FIELDS_H_

#include <string>
#include <string_view>

#include "xff/vfs/entry.h"

namespace xff::fields {

// Renders `tmpl` against one entry, substituting {field} placeholders with
// values derived from `path`, `metadata`, and `depth`. `{{` and `}}` emit
// literal braces; an unterminated `{` is literal and an unknown field renders
// empty. This is the foundation for the --format template and (gated) -exec
// substitution.
//
// Supported: {path} {dir} {name}/{file} {stem} {ext}/{extension} {depth} {size}
// {type} {inode} {links}, and time fields {atime} {mtime} {ctime} {btime} with
// an optional qualifier {field:QUAL} -- a strftime format ({mtime:%Y-%m-%d}) or
// preset ({mtime:iso|epoch}); local time, default ISO-8601. The quoted-string
// qualifier, owner/mode fields, {root}, and {suffixes} layer on later.
std::string Render(std::string_view tmpl, std::string_view path, const vfs::Metadata& metadata, int depth);

}  // namespace xff::fields

#endif  // XFF_FIELDS_FIELDS_H_
