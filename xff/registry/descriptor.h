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

#ifndef XFF_REGISTRY_DESCRIPTOR_H_
#define XFF_REGISTRY_DESCRIPTOR_H_

#include <string_view>

namespace xff::registry {

// Where a token lives on the command line (design.md "CLI grammar & parser").
enum class Region { kGlobal, kExpression };

// What an expression token is.
enum class Kind { kTest, kAction, kOperator };

// Safety classification, surfaced in --help / --explain (design.md "Security & safety").
enum class Safety { kNone, kSafety, kSecurity };

// Cost tier driving the advisory ordering warning (design.md "Evaluation").
enum class Cost { kCheap, kMeta, kExpensive };

// One option / predicate / action description. The registry is the single
// source of truth from which the parser, --help, completions, --explain, and
// the cost-warning are all derived.
struct Descriptor {
  std::string_view name;
  Kind kind = Kind::kTest;
  Region region = Region::kExpression;
  int arity = 0;  // tokens consumed as arguments
  Safety safety = Safety::kNone;
  Cost cost = Cost::kCheap;
  bool pure = true;  // side-effect-free (reorderable within a conjunction)
};

}  // namespace xff::registry

#endif  // XFF_REGISTRY_DESCRIPTOR_H_
