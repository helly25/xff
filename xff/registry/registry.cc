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

#include "xff/registry/registry.h"

#include <array>
#include <string_view>

#include "xff/registry/descriptor.h"

namespace xff::registry {
namespace {

// The initial slice of the find expression vocabulary. Phase 1 grows this
// (size/time/perm/exec/...); the parser, --help, --explain and the
// cost-warning all read from here.
constexpr std::array kDescriptors = std::to_array<Descriptor>({
    {.name = "-name", .kind = Kind::kTest, .arity = 1},
    {.name = "-iname", .kind = Kind::kTest, .arity = 1},
    {.name = "-path", .kind = Kind::kTest, .arity = 1},
    {.name = "-ipath", .kind = Kind::kTest, .arity = 1},
    {.name = "-type", .kind = Kind::kTest, .arity = 1},
    {.name = "-size", .kind = Kind::kTest, .arity = 1},
    {.name = "-links", .kind = Kind::kTest, .arity = 1},
    {.name = "-perm", .kind = Kind::kTest, .arity = 1},
    {.name = "-maxdepth", .kind = Kind::kTest, .arity = 1},
    {.name = "-mindepth", .kind = Kind::kTest, .arity = 1},
    {.name = "-empty", .kind = Kind::kTest, .arity = 0},
    {.name = "-true", .kind = Kind::kTest, .arity = 0},
    {.name = "-false", .kind = Kind::kTest, .arity = 0},
    {.name = "-print", .kind = Kind::kAction, .arity = 0},
    {.name = "-print0", .kind = Kind::kAction, .arity = 0},
    {.name = "-a", .kind = Kind::kOperator, .arity = 0},
    {.name = "-and", .kind = Kind::kOperator, .arity = 0},
    {.name = "-o", .kind = Kind::kOperator, .arity = 0},
    {.name = "-or", .kind = Kind::kOperator, .arity = 0},
    {.name = "-not", .kind = Kind::kOperator, .arity = 0},
    {.name = "!", .kind = Kind::kOperator, .arity = 0},
});

}  // namespace

const Descriptor* Lookup(std::string_view name) {
  for (const Descriptor& descriptor : kDescriptors) {
    if (descriptor.name == name) {
      return &descriptor;
    }
  }
  return nullptr;
}

}  // namespace xff::registry
