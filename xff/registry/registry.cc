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
    {.name = "-iname", .kind = Kind::kTest, .arity = 1, .fold_case = true},
    {.name = "-path", .kind = Kind::kTest, .arity = 1},
    {.name = "-ipath", .kind = Kind::kTest, .arity = 1, .fold_case = true},
    {.name = "-wholename", .kind = Kind::kTest, .arity = 1},                      // GNU synonym for -path
    {.name = "-iwholename", .kind = Kind::kTest, .arity = 1, .fold_case = true},  // GNU synonym for -ipath
    {.name = "-lname", .kind = Kind::kTest, .arity = 1},                          // glob the symlink target
    {.name = "-ilname", .kind = Kind::kTest, .arity = 1, .fold_case = true},
    {.name = "-regex", .kind = Kind::kTest, .arity = 1},
    {.name = "-iregex", .kind = Kind::kTest, .arity = 1, .fold_case = true},
    {.name = "-regextype", .kind = Kind::kTest, .arity = 1},
    {.name = "-type", .kind = Kind::kTest, .arity = 1},
    {.name = "-xtype", .kind = Kind::kTest, .arity = 1},  // -type of the symlink's target
    {.name = "-size", .kind = Kind::kTest, .arity = 1},
    {.name = "-links", .kind = Kind::kTest, .arity = 1},
    {.name = "-inum", .kind = Kind::kTest, .arity = 1},
    {.name = "-samefile", .kind = Kind::kTest, .arity = 1},
    {.name = "-fstype", .kind = Kind::kTest, .arity = 1},  // filesystem type name (statfs)
    {.name = "-uid", .kind = Kind::kTest, .arity = 1},
    {.name = "-gid", .kind = Kind::kTest, .arity = 1},
    {.name = "-user", .kind = Kind::kTest, .arity = 1},
    {.name = "-group", .kind = Kind::kTest, .arity = 1},
    {.name = "-nouser", .kind = Kind::kTest, .arity = 0},   // owner uid has no passwd entry
    {.name = "-nogroup", .kind = Kind::kTest, .arity = 0},  // group gid has no group entry
    {.name = "-newer", .kind = Kind::kTest, .arity = 1},
    {.name = "-anewer", .kind = Kind::kTest, .arity = 1},  // atime newer than ref's mtime (== -neweram)
    {.name = "-cnewer", .kind = Kind::kTest, .arity = 1},  // ctime newer than ref's mtime (== -newercm)
    {.name = "-neweraa", .kind = Kind::kTest, .arity = 1},
    {.name = "-newerac", .kind = Kind::kTest, .arity = 1},
    {.name = "-neweram", .kind = Kind::kTest, .arity = 1},
    {.name = "-newerca", .kind = Kind::kTest, .arity = 1},
    {.name = "-newercc", .kind = Kind::kTest, .arity = 1},
    {.name = "-newercm", .kind = Kind::kTest, .arity = 1},
    {.name = "-newerma", .kind = Kind::kTest, .arity = 1},
    {.name = "-newermc", .kind = Kind::kTest, .arity = 1},
    {.name = "-newermm", .kind = Kind::kTest, .arity = 1},
    {.name = "-newerat", .kind = Kind::kTest, .arity = 1},  // atime newer than a time string
    {.name = "-newerct", .kind = Kind::kTest, .arity = 1},  // ctime newer than a time string
    {.name = "-newermt", .kind = Kind::kTest, .arity = 1},  // mtime newer than a time string
    {.name = "-mtime", .kind = Kind::kTest, .arity = 1},
    {.name = "-mmin", .kind = Kind::kTest, .arity = 1},
    {.name = "-atime", .kind = Kind::kTest, .arity = 1},
    {.name = "-amin", .kind = Kind::kTest, .arity = 1},
    {.name = "-ctime", .kind = Kind::kTest, .arity = 1},
    {.name = "-cmin", .kind = Kind::kTest, .arity = 1},
    {.name = "-used", .kind = Kind::kTest, .arity = 1},  // days between atime and ctime
    {.name = "-perm", .kind = Kind::kTest, .arity = 1},
    {.name = "-maxdepth", .kind = Kind::kTest, .arity = 1},
    {.name = "-mindepth", .kind = Kind::kTest, .arity = 1},
    {.name = "-depth", .kind = Kind::kTest, .arity = 0},
    {.name = "-d", .kind = Kind::kTest, .arity = 0},  // BSD/GNU short spelling of -depth
    {.name = "-xdev", .kind = Kind::kTest, .arity = 0},
    {.name = "-mount", .kind = Kind::kTest, .arity = 0},     // GNU/BSD synonym for -xdev
    {.name = "-x", .kind = Kind::kTest, .arity = 0},         // BSD synonym for -xdev
    {.name = "-daystart", .kind = Kind::kTest, .arity = 0},  // measure age tests from today's local midnight
    {.name = "-empty", .kind = Kind::kTest, .arity = 0},
    {.name = "-sparse", .kind = Kind::kTest, .arity = 0},  // file has holes (blocks*512 < size)
    {.name = "-readable", .kind = Kind::kTest, .arity = 0},
    {.name = "-writable", .kind = Kind::kTest, .arity = 0},
    {.name = "-executable", .kind = Kind::kTest, .arity = 0},
    {.name = "-true", .kind = Kind::kTest, .arity = 0},
    {.name = "-false", .kind = Kind::kTest, .arity = 0},
    {.name = "-ls", .kind = Kind::kAction, .arity = 0},  // ls -dils style line per entry
    {.name = "-print", .kind = Kind::kAction, .arity = 0},
    {.name = "-print0", .kind = Kind::kAction, .arity = 0},
    {.name = "-printf", .kind = Kind::kAction, .arity = 1},
    // xff: -print with the OS line ending
    {.name = "-println", .kind = Kind::kAction, .arity = 0, .style = Style::kXff},
    // xff: -printf + the OS line ending
    {.name = "-printfln", .kind = Kind::kAction, .arity = 1, .style = Style::kXff},
    {.name = "-fprint", .kind = Kind::kAction, .arity = 1},   // -print to a named FILE
    {.name = "-fprint0", .kind = Kind::kAction, .arity = 1},  // -print0 to a named FILE
    {.name = "-fprintf", .kind = Kind::kAction, .arity = 2},  // -printf FORMAT to a named FILE
    {.name = "-fls", .kind = Kind::kAction, .arity = 1},      // -ls to a named FILE
    {.name = "-delete", .kind = Kind::kAction, .arity = 0, .safety = Safety::kSafety},
    {.name = "-prune", .kind = Kind::kAction, .arity = 0},
    {.name = "-quit", .kind = Kind::kAction, .arity = 0},
    {.name = "-exec", .kind = Kind::kAction, .arity = -1, .safety = Safety::kSecurity},
    // -exec in the matched entry's directory
    {.name = "-execdir", .kind = Kind::kAction, .arity = -1, .safety = Safety::kSecurity},
    // -exec that prompts; runs only on an affirmative reply
    {.name = "-ok", .kind = Kind::kAction, .arity = -1, .safety = Safety::kSecurity},
    // -execdir that prompts; runs only on an affirmative reply
    {.name = "-okdir", .kind = Kind::kAction, .arity = -1, .safety = Safety::kSecurity},
    // -capture=NAME[=REGEX] cmd... ;
    {
        .name = "-capture",
        .kind = Kind::kAction,
        .arity = -1,
        .binding = Binding::kLabelRegex,
        .safety = Safety::kSecurity,
        .style = Style::kXff,
    },
    // -capture run in the matched entry's directory
    {
        .name = "-capturedir",
        .kind = Kind::kAction,
        .arity = -1,
        .binding = Binding::kLabelRegex,
        .safety = Safety::kSecurity,
        .style = Style::kXff,
    },
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
