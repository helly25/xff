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

#include "xff/archive/member_path.h"

#include <optional>
#include <string>
#include <string_view>

#include "absl/functional/function_ref.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"

namespace xff::archive {

namespace {

// The lower-cased extension of `container`, without the dot ("phar", "jar", ""). Only the last one
// counts: `a.tar.gz` is "gz", which claims nothing, so it takes the generic scheme.
std::string ContainerExtension(std::string_view container) {
  const std::string_view::size_type dot = container.find_last_of('.');
  const std::string_view::size_type slash = container.find_last_of('/');
  if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) {
    return "";
  }
  return absl::AsciiStrToLower(container.substr(dot + 1));
}

}  // namespace

std::string JoinMemberPath(std::string_view container, std::string_view member, const MemberPathOptions& options) {
  // Plain concatenation, deliberately: the member is reproduced exactly as the archive stored it, so
  // an absolute member keeps its leading slash (`a.tgz!/rooted`) instead of being normalized away.
  if (options.prefix != kUriPrefix) {
    // Empty prefix -> a bare path; any other string is prepended verbatim.
    return absl::StrCat(options.prefix, container, options.separator, member);
  }
  // Where an ecosystem OWNS the spelling, use its own - a URI exists to be handed to a tool that
  // will parse it, and PHP and Java each parse only their form. Both fix the separator too, so
  // `--archive-separator` does not apply to these two.
  const std::string extension = ContainerExtension(container);
  if (extension == "phar") {
    // `phar:///abs/a.phar/inner`: the scheme already ends in `//`, so an absolute container's own
    // leading slash completes the empty authority, exactly as the generic branch does by hand.
    return absl::StrCat(kPharScheme, container, "/", member);
  }
  if (extension == "jar" || extension == "war" || extension == "ear") {
    return absl::StrCat(kJarScheme, container, kJarSeparator, member);
  }
  // `//` is the URI authority marker, so it may only precede an ABSOLUTE path (leaving the authority
  // empty, as `file:///...` does). A relative container takes the opaque form instead; writing
  // `archive://a.tgz` would declare `a.tgz` a host name.
  const std::string_view authority = container.starts_with('/') ? kUriAuthority : std::string_view{};
  return absl::StrCat(kUriScheme, authority, container, options.separator, member);
}

namespace {

// Strips whatever `options.prefix` says this path carries. Returns false when the required prefix is
// absent: the flag states what the spelling IS, so a bare path must not also be accepted.
bool StripPrefix(std::string_view& path, const MemberPathOptions& options) {
  if (options.prefix == kUriPrefix) {
    if (!path.starts_with(kUriScheme)) {
      return false;
    }
    path.remove_prefix(kUriScheme.size());
    // Accept both forms Join renders: `archive://<abs>` and the opaque `archive:<relative>`.
    if (path.starts_with(kUriAuthority)) {
      path.remove_prefix(kUriAuthority.size());
    }
    return true;
  }
  if (options.prefix.empty()) {
    return true;
  }
  if (!path.starts_with(options.prefix)) {
    return false;
  }
  path.remove_prefix(options.prefix.size());
  return true;
}

// The inverse of the two ecosystem spellings Join emits. They carry their own separator, so they are
// split before the generic path, and only when the prefix asks for URI rendering.
std::optional<MemberPathParts> SplitEcosystemUri(std::string_view path) {
  if (path.starts_with(kJarScheme)) {
    const std::string_view rest = path.substr(kJarScheme.size());
    const std::string_view::size_type at = rest.find(kJarSeparator);
    if (at == std::string_view::npos) {
      return std::nullopt;
    }
    return MemberPathParts{.container = rest.substr(0, at), .member = rest.substr(at + kJarSeparator.size())};
  }
  if (path.starts_with(kPharScheme)) {
    const std::string_view rest = path.substr(kPharScheme.size());
    // The container ends at the `.phar` component, since phar's own URL uses a plain `/` from there
    // on. Same heuristic class as `!` in a bare path: a directory could be named `x.phar`, which is
    // rare, and a walk resolves the truth regardless.
    static constexpr std::string_view kPharBoundary = ".phar/";
    const std::string_view::size_type at = rest.find(kPharBoundary);
    if (at == std::string_view::npos) {
      return std::nullopt;
    }
    const std::string_view::size_type member_at = at + kPharBoundary.size();
    return MemberPathParts{.container = rest.substr(0, member_at - 1), .member = rest.substr(member_at)};
  }
  return std::nullopt;
}
}  // namespace

std::optional<MemberPathParts> SplitMemberPath(std::string_view path, const MemberPathOptions& options) {
  if (options.prefix == kUriPrefix) {
    if (const std::optional<MemberPathParts> parts = SplitEcosystemUri(path); parts.has_value()) {
      return parts;
    }
  }
  if (options.separator.empty()) {
    // An empty separator would make every path a member path with an empty container.
    return std::nullopt;
  }
  if (options.separator.find_first_not_of('/') == std::string_view::npos) {
    // A separator made ONLY of slashes turns every directory boundary into a candidate, so the
    // container boundary is not decidable from the string: `/abs/a.phar/inner/x` would split at the
    // leading slash. Refuse rather than answer wrongly - the probing overload resolves it the way a
    // walk does. Note `!/` and `#/` are NOT affected: their `!` / `#` still distinguishes them from
    // a plain boundary, so first-occurrence works there exactly as it does for `!`.
    return std::nullopt;
  }
  if (!StripPrefix(path, options)) {
    return std::nullopt;
  }
  const std::string_view::size_type at = path.find(options.separator);
  if (at == std::string_view::npos) {
    return std::nullopt;
  }
  // First occurrence wins and the remainder is verbatim, so a member that itself contains the
  // separator stays intact and a path xff printed round-trips unchanged.
  return MemberPathParts{
      .container = path.substr(0, at),
      .member = path.substr(at + options.separator.size()),
  };
}

std::optional<MemberPathParts> SplitMemberPath(
    std::string_view path,
    const MemberPathOptions& options,
    absl::FunctionRef<bool(std::string_view)> is_container) {
  // The ecosystem URLs carry their own separator, so they are recognised here too - the walk uses
  // THIS overload to map a rendered path back to a member, and a form only the other overload knew
  // would render paths the walk could not then look up.
  if (options.prefix == kUriPrefix) {
    if (const std::optional<MemberPathParts> parts = SplitEcosystemUri(path);
        parts.has_value() && is_container(parts->container)) {
      return parts;
    }
  }
  if (options.separator.empty()) {
    return std::nullopt;
  }
  if (!StripPrefix(path, options)) {
    return std::nullopt;
  }
  // Try every separator occurrence left to right and let the filesystem decide, exactly as a walk
  // does: the first prefix that IS an openable archive is the container. Left to right means the
  // OUTERMOST container wins, which is the same outside-in order --archive-depth nests in.
  for (std::string_view::size_type at = path.find(options.separator); at != std::string_view::npos;
       at = path.find(options.separator, at + 1)) {
    const std::string_view container = path.substr(0, at);
    if (container.empty()) {
      continue;  // a leading separator names no container
    }
    if (is_container(container)) {
      return MemberPathParts{
          .container = container,
          .member = path.substr(at + options.separator.size()),
      };
    }
  }
  return std::nullopt;
}

bool IsMemberPath(std::string_view path, const MemberPathOptions& options) {
  return SplitMemberPath(path, options).has_value();
}

std::string_view NormalizeMemberName(std::string_view member) {
  while (member.starts_with("./")) {
    member.remove_prefix(2);
  }
  while (member.size() > 1 && member.ends_with('/')) {
    member.remove_suffix(1);
  }
  return member;
}

}  // namespace xff::archive
