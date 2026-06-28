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

// How a primary carries an attached '=' payload on its own token, so the parser
// reads the grammar from the registry instead of hardcoding names (design #68):
// kNone for most; kLabelRegex for -capture/-capturedir (-capture=NAME[=REGEX]).
enum class Binding { kNone, kLabel, kLabelRegex };

// Whether a primary belongs to find (kFind) or is an xff extension (kXff). The
// strict find style (--config=find) rejects xff extensions; the xff style accepts
// all. The default is kFind, so only xff-native primaries need tagging.
enum class Style { kFind, kXff };

// One option / predicate / action description. The registry is the single
// source of truth from which the parser, --help, completions, --explain, and
// the cost-warning are all derived.
struct Descriptor {
  std::string_view name;
  // One-line synopsis for `xff help <name>` and the generated --help listing: a
  // short phrase, normally lower-case (proper nouns such as GNU/BSD aside), with no
  // trailing period (e.g. "match the basename against a shell glob"). Every
  // descriptor carries one; registry_test enforces the shape.
  std::string_view summary;
  Kind kind = Kind::kTest;
  Region region = Region::kExpression;
  int arity = 0;                     // trailing tokens consumed as arguments (-1 = variadic until ';')
  Binding binding = Binding::kNone;  // attached '=' payload carried on the token itself
  // The case-insensitive variant of a matcher (-iname/-ipath/-iregex): folds case
  // when matching (FNM_CASEFOLD for the glob tests, RE2 case-insensitive for the
  // regex). Lets the parser/evaluator read case from the registry instead of
  // keying off the leading 'i' in the primary's name.
  bool fold_case = false;
  Safety safety = Safety::kNone;
  Style style = Style::kFind;  // find-native by default; set kXff to mark an xff extension
  Cost cost = Cost::kCheap;
  bool pure = true;  // side-effect-free (reorderable within a conjunction)
};

}  // namespace xff::registry

#endif  // XFF_REGISTRY_DESCRIPTOR_H_
