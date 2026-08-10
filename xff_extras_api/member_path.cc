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

#include "xff/archive/member_path.h"

#include <optional>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"

namespace xff::archive {

std::string JoinMemberPath(std::string_view container, std::string_view member, const MemberPathOptions& options) {
  // Plain concatenation, deliberately: the member is reproduced exactly as the archive stored it, so
  // an absolute member keeps its leading slash (`a.tgz!/rooted`) instead of being normalized away.
  const std::string_view scheme = options.prefix == PathPrefix::kUri ? kUriScheme : std::string_view{};
  return absl::StrCat(scheme, container, options.separator, member);
}

std::optional<MemberPathParts> SplitMemberPath(std::string_view path, const MemberPathOptions& options) {
  if (options.separator.empty()) {
    // An empty separator would make every path a member path with an empty container.
    return std::nullopt;
  }
  if (options.prefix == PathPrefix::kUri) {
    if (!path.starts_with(kUriScheme)) {
      return std::nullopt;
    }
    path.remove_prefix(kUriScheme.size());
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

bool IsMemberPath(std::string_view path, const MemberPathOptions& options) {
  return SplitMemberPath(path, options).has_value();
}

}  // namespace xff::archive
