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

#ifndef XFF_EXEC_EXEC_H_
#define XFF_EXEC_EXEC_H_

#include <string>
#include <string_view>
#include <vector>

namespace xff::exec {

// Runs find's `-exec command... ;` for one entry: every "{}" substring in each
// token of `command` is replaced by `path`, then the command is spawned (PATH-
// searched via posix_spawnp) and waited for. Returns true iff the child ran and
// exited 0, matching find's -exec truth value. Synchronous and serial; the `+`
// batch form, -execdir, and -j parallelism are layered on separately.
bool Execute(const std::vector<std::string>& command, std::string_view path);

}  // namespace xff::exec

#endif  // XFF_EXEC_EXEC_H_
