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

#ifndef XFF_ARCHIVE_MEMBER_PATH_H_
#define XFF_ARCHIVE_MEMBER_PATH_H_

// How an archive member is spelled as one path: `<container><separator><member>`.
//
// There is no single ecosystem convention - `!` (JAR / Java URLs), `#` (fragment style), the
// multi-character `!/` and `#/` other tools print, and URI forms all exist - so the spelling is a
// presentation choice under user control (`--archive-separator`, `--archive-prefix`), never
// hard-coded. This lives in the shared extras API because BOTH sides need it: the archive extra
// renders member paths, and the core parses one handed back in (a `-cmp` target, a `{def.X}`), and
// an extra must not depend on the core.
//
// The composition rule is deliberately dumb: plain concatenation, with the member reproduced
// exactly as the archive stored it. xff adds and removes no slash, because an archive may
// legitimately contain ABSOLUTE member paths and hiding that would hide the Zip-Slip red flag:
//
//   separator `!`  ->  `a.tgz!relative`   and  `a.tgz!/rooted`
//   separator `!/` ->  `a.tgz!/relative`  and  `a.tgz!//rooted`   (the doubled slash is correct)
//
// Splitting is the exact inverse: cut at the FIRST occurrence of the separator and take the
// remainder verbatim, so a path xff printed round-trips through xff unchanged.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace xff::archive {

// The default separator: a plain `!`, as JAR and Java archive URLs use. Preferred over `!/` because
// an absolute member then reads as the odd-looking `!//` (see the header comment).
inline constexpr std::string_view kDefaultSeparator = "!";

// The URI scheme used by the `URI` prefix keyword. Generic on purpose for now; whether it should instead be
// per-format (`tar:` / `zip:`) or wrap the container as Java's `jar:file:/...!/...` is still open
// (see TODO.md), and only this constant plus its docs change when it is settled.
//
// Note the deliberate absence of `//`: in a URI `//` introduces the AUTHORITY, so `archive://a.tgz!x`
// would parse `a.tgz` as a HOST NAME rather than a path. An absolute container therefore renders as
// `archive:///abs/a.tar!x` - empty authority, then the absolute path, exactly as `file:///...` does -
// and a relative container as the opaque `archive:a.tgz!x`. Both are well-formed; one blanket
// `archive://` prefix is not.
inline constexpr std::string_view kUriScheme = "archive:";

// The authority marker inserted after the scheme when, and only when, the container is absolute.
inline constexpr std::string_view kUriAuthority = "//";

// The one KEYWORD `prefix` accepts, spelled in caps like xff's other keyword values (`RE2`,
// `PCRE2`, `GLOB`). It has to be a keyword rather than a literal because URI rendering carries
// LOGIC no literal can express: the `//` authority marker appears only for an absolute container.
// Caps also keep it apart from a literal prefix, which is why "none" is NOT a value here - it
// would be indistinguishable from a literal prefix spelled `none`. Empty means no prefix.
inline constexpr std::string_view kUriPrefix = "URI";

struct MemberPathOptions {
  // Any string, not a fixed menu, so xff can emit what another system accepts.
  std::string_view separator = kDefaultSeparator;
  // EMPTY (the default) means no prefix. `kUriPrefix` ("URI") selects URI rendering. Any other
  // string is used LITERALLY, so a system expecting its own marker can be fed one without a code
  // change - the same "any string" freedom `separator` has.
  std::string_view prefix = {};
};

// A member path split into its two halves. `member` is verbatim, including any leading slash.
struct MemberPathParts {
  std::string_view container;
  std::string_view member;
};

// `container` + separator + `member`, prefixed per `options`. No slash is added or removed.
[[nodiscard]] std::string JoinMemberPath(
    std::string_view container,
    std::string_view member,
    const MemberPathOptions& options = {});

// The inverse of JoinMemberPath: splits at the FIRST separator, after stripping the URI scheme when
// `options.prefix` asks for it. Returns nullopt when `path` holds no separator (an ordinary path),
// or when the separator is empty (which would make every path a member path).
[[nodiscard]] std::optional<MemberPathParts> SplitMemberPath(
    std::string_view path,
    const MemberPathOptions& options = {});

// True when `path` spells a member path under `options` (i.e. SplitMemberPath would succeed).
[[nodiscard]] bool IsMemberPath(std::string_view path, const MemberPathOptions& options = {});

}  // namespace xff::archive

#endif  // XFF_ARCHIVE_MEMBER_PATH_H_
