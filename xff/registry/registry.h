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

#ifndef XFF_REGISTRY_REGISTRY_H_
#define XFF_REGISTRY_REGISTRY_H_

#include <string_view>

#include "xff/registry/descriptor.h"

namespace xff::registry {

// Looks up the descriptor for a command-line expression token (e.g. "-name",
// "-type", "-o", "!"). Returns nullptr if the token is not a known
// predicate / action / operator.
const Descriptor* Lookup(std::string_view name);

}  // namespace xff::registry

#endif  // XFF_REGISTRY_REGISTRY_H_
