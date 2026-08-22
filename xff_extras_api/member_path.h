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
//
// One nuance, and it is an INCONVENIENCE rather than a flaw: a separator that can also occur inside a
// container path - `/` above all - cannot be located by string inspection alone, since `/abs/a.phar`
// contains slashes of its own. A bare `/` is nonetheless a perfectly legitimate spelling; the visible
// cues `!` and `#` are conventions people adopted, not a correctness requirement. What `/` needs is
// filesystem CONTEXT, which a traversal has for free: walking top-down, xff meets `a.phar` as a real
// FILE, sniffs it, and descends - so nothing is ambiguous there, and no directory can share the name
// (one path is either a file or a directory, never both). Offline the same answer comes from probing:
// the longest leading component sequence that IS an openable archive is the container. Hence two
// overloads below - the pure-string one, and one taking that probe.

#include <optional>
#include <string>
#include <string_view>

#include "absl/functional/function_ref.h"

namespace xff::archive {

// The default separator: a plain `!`, as JAR and Java archive URLs use. Preferred over `!/` because
// an absolute member then reads as the odd-looking `!//` (see the header comment).
inline constexpr std::string_view kDefaultSeparator = "!";

// The URI scheme used by the `URI` prefix keyword for a container no ecosystem has claimed. Two do,
// and the point of a URI is that the receiving tool accepts it, so those are rendered their way
// (see UriSchemeFor): a `.phar` as PHP's `phar://`, a `.jar` / `.war` / `.ear` as Java's
// `jar:file:...!/`. The choice is by EXTENSION, not by sniffed format, because that is what the
// claim is: a jar IS a zip, and only its name says which convention its readers expect.
//
// The BARE path is unaffected and keeps one separator whatever the container is (see the header
// comment): being readable and re-pasteable is worth more there than matching a foreign spelling,
// and phar's `/` would make `a.phar/inner/x` ambiguous. A URI is the opposite trade - it exists to
// be handed to someone else - which is why the format-specific spelling lives on this axis only.
//
// Note the deliberate absence of `//`: in a URI `//` introduces the AUTHORITY, so `archive://a.tgz!x`
// would parse `a.tgz` as a HOST NAME rather than a path. An absolute container therefore renders as
// `archive:///abs/a.tar!x` - empty authority, then the absolute path, exactly as `file:///...` does -
// and a relative container as the opaque `archive:a.tgz!x`. Both are well-formed; one blanket
// `archive://` prefix is not.
inline constexpr std::string_view kUriScheme = "archive:";

// PHP spells a phar member `phar:///abs/a.phar/inner/x`: its own scheme, and a plain `/` between
// container and member rather than a marker. The separator is part of the convention, so the URI
// rendering uses it and ignores `--archive-separator` for this container kind.
inline constexpr std::string_view kPharScheme = "phar://";

// Java spells a jar member `jar:file:/abs/a.jar!/inner`: a nested URL, then `!/`. Both halves are
// fixed by the JAR URL syntax, so the same reasoning applies.
inline constexpr std::string_view kJarScheme = "jar:file:";
inline constexpr std::string_view kJarSeparator = "!/";

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
  // EMPTY (the default - a default-constructed view) means no prefix. `kUriPrefix` ("URI") selects
  // URI rendering. Any other string is used LITERALLY, so a system expecting its own marker can be
  // fed one without a code change - the same "any string" freedom `separator` has.
  std::string_view prefix;
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

// The inverse of JoinMemberPath: splits at the FIRST separator, after stripping the prefix when
// `options.prefix` asks for it. Returns nullopt when `path` holds no separator (an ordinary path),
// or when the separator is empty (which would make every path a member path).
//
// ALSO returns nullopt when the separator is made ONLY of slashes (`/`, `//`). Not because such a
// spelling is wrong, but
// because THIS function has no filesystem context: `/abs/a.phar/inner/x` under separator `/` would cut
// at the leading slash and report an empty container, because every directory boundary looks like a
// separator. The boundary is perfectly well defined, just not from the string - so use the probing
// overload below, which resolves it the way a walk does. Refusing here rather than guessing keeps a
// wrong split from passing for a right one.
//
// `!`, `#`, `!/` and `#/` are unaffected, but be precise about why: they are not impossible in a real
// path - a directory really can be named `foo!` - so first-occurrence is a HEURISTIC there. It is
// accepted because such names are rare, and because a walk resolves the truth regardless: a path
// containing a literal `!/` simply is not an archive, so the walk finds nothing to descend and the
// split point only ever served as a convenient marker.
[[nodiscard]] std::optional<MemberPathParts> SplitMemberPath(
    std::string_view path,
    const MemberPathOptions& options = {});

// Splits with a filesystem oracle, which is what makes an all-slash separator work. `is_container` answers "is this
// path a file xff can open as an archive?" - during a walk that is already known; offline it is a stat plus a format
// sniff. Each separator occurrence is tried left to right and the FIRST prefix the probe accepts wins, so the outermost
// container is chosen, matching --archive-depth's outside-in nesting.
[[nodiscard]] std::optional<MemberPathParts> SplitMemberPath(
    std::string_view path,
    const MemberPathOptions& options,
    absl::FunctionRef<bool(std::string_view)> is_container);

// True when `path` spells a member path under `options` (i.e. SplitMemberPath would succeed).
[[nodiscard]] bool IsMemberPath(std::string_view path, const MemberPathOptions& options = {});

// A MEMBER NAME (the part after the separator) in comparable form: without any leading `./` and
// without a trailing `/`. Containers spell the same member several ways, so neither the stored name
// nor the name a user typed can be authoritative - a reader has to normalize both sides before
// comparing them:
//   - tar writes both `dir/x` and `./dir/x` for one member, depending on how it was created;
//   - a DIRECTORY member is spelled `dir/` in tar and phar alike, so a lookup for `dir` must find
//     it, and then report that it has no content rather than that it does not exist.
// Root (`/`) is left alone rather than reduced to the empty name.
[[nodiscard]] std::string_view NormalizeMemberName(std::string_view member);

}  // namespace xff::archive

#endif  // XFF_ARCHIVE_MEMBER_PATH_H_
