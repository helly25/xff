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

#ifndef XFF_PARSER_AST_H_
#define XFF_PARSER_AST_H_

#include <string>
#include <vector>

namespace xff::parser {

// Result of splitting argv per the xff grammar (design.md "CLI grammar &
// parser"): order-independent globals, one or more search roots, then the
// position-dependent find expression. Skeleton: globals and expression are
// raw tokens for now; the declarative registry + recursive-descent expression
// parser come next.
struct Command {
  std::vector<std::string> globals;
  std::vector<std::string> roots;
  std::vector<std::string> expression;
};

}  // namespace xff::parser

#endif  // XFF_PARSER_AST_H_
