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

#include "xff/license/notice.h"

#include <vector>

#include "absl/algorithm/container.h"

namespace xff::license {
namespace {

// The registered notices. A function-local static (constructed on first use) so the file-scope
// Registrars -- core deps in xff/license, each build-extra in its own TU -- can register during
// static init without a static-init-order dependency on a namespace-scope container.
std::vector<Notice>& Registry() {
  static std::vector<Notice> registry;
  return registry;
}

}  // namespace

void Register(Notice notice) {
  Registry().push_back(notice);
}

std::vector<Notice> Notices() {
  std::vector<Notice> out = Registry();
  absl::c_sort(out, [](const Notice& lhs, const Notice& rhs) { return lhs.component < rhs.component; });
  return out;
}

}  // namespace xff::license
