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

#ifndef XFF_PARSER_PARSER_H_
#define XFF_PARSER_PARSER_H_

#include <string>
#include <vector>

#include "xff/parser/ast.h"

namespace xff::parser {

// Splits argv into [globals] [roots] [expression] per the xff grammar:
// order-independent global flags (leading '-'/'+' tokens) up to the first
// directory or an explicit '--' boundary; one or more roots; then the
// position-dependent find expression (parsed in full in a later phase).
Command Parse(const std::vector<std::string>& args);

}  // namespace xff::parser

#endif  // XFF_PARSER_PARSER_H_
