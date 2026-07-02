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

#include "absl/types/span.h"
#include "xff/registry/descriptor.h"

namespace xff::registry {
namespace {

// The find / xff expression vocabulary. The parser, --help, `xff help <name>`,
// --explain and the cost-warning all read from here; `summary` is the one-line
// synopsis surfaced by the help system.
constexpr std::array kDescriptors = std::to_array<Descriptor>({
    {
        .name = "-name",
        .summary = "match the basename against a shell glob",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-iname",
        .summary = "match the basename against a shell glob, case-insensitively",
        .kind = Kind::kTest,
        .arity = 1,
        .fold_case = true,
    },
    {
        .name = "-path",
        .summary = "match the whole path against a shell glob",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-ipath",
        .summary = "match the whole path against a shell glob, case-insensitively",
        .kind = Kind::kTest,
        .arity = 1,
        .fold_case = true,
    },
    {
        .name = "-wholename",
        .summary = "GNU synonym for -path",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-iwholename",
        .summary = "GNU synonym for -ipath",
        .kind = Kind::kTest,
        .arity = 1,
        .fold_case = true,
    },
    {
        .name = "-lname",
        .summary = "match the symlink target against a shell glob",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-ilname",
        .summary = "match the symlink target against a shell glob, case-insensitively",
        .kind = Kind::kTest,
        .arity = 1,
        .fold_case = true,
    },
    {
        .name = "-regex",
        .summary = "match the whole path against a regular expression",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-iregex",
        .summary = "match the whole path against a regular expression, case-insensitively",
        .kind = Kind::kTest,
        .arity = 1,
        .fold_case = true,
    },
    {
        .name = "-regextype",
        .summary = "select the regex dialect for the following -regex/-iregex",
        .kind = Kind::kTest,
        .arity = 1,
    },
    // xff content-search predicates: match the file's CONTENT, not its path. The
    // literal pair (-content/-icontent) sidesteps grep's regex-flavor ambiguity; the
    // regex pair (-rxc/-irxc) is the RE2 counterpart. All read the file (skipping
    // binaries), so they are Cost::kExpensive and the strict find style rejects them.
    {
        .name = "-content",
        .summary = "match a literal substring in the file's content (xff)",
        .kind = Kind::kTest,
        .arity = 1,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
    },
    {
        .name = "-icontent",
        .summary = "match a literal substring in the file's content, case-insensitively (xff)",
        .kind = Kind::kTest,
        .arity = 1,
        .fold_case = true,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
    },
    {
        .name = "-rxc",
        .summary = "match the file's content against a regular expression (xff)",
        .kind = Kind::kTest,
        .arity = 1,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
    },
    {
        .name = "-irxc",
        .summary = "match the file's content against a regular expression, case-insensitively (xff)",
        .kind = Kind::kTest,
        .arity = 1,
        .fold_case = true,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
    },
    {
        .name = "-type",
        .summary = "match the file type (f/d/l/b/c/p/s)",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-xtype",
        .summary = "match the file type of a symlink's target",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-size",
        .summary = "match the apparent size (unit suffix c/w/k/M/G/T/P/E)",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        // xff extension: -size but over ALLOCATED space (st_blocks), not apparent size.
        .name = "-blocks",
        .summary = "match the allocated size (st_blocks); xff's disk-occupancy counterpart to -size",
        .kind = Kind::kTest,
        .arity = 1,
        .style = Style::kXff,
    },
    {
        .name = "-links",
        .summary = "match the hard-link count",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-inum",
        .summary = "match the inode number",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-samefile",
        .summary = "match files that share an inode with FILE",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-fstype",
        .summary = "match the filesystem type (statfs)",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-uid",
        .summary = "match the numeric owner id",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-gid",
        .summary = "match the numeric group id",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-user",
        .summary = "match the owner by name",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-group",
        .summary = "match the group by name",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-nouser",
        .summary = "match when the owner uid has no passwd entry",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-nogroup",
        .summary = "match when the group gid has no group entry",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-newer",
        .summary = "match when mtime is newer than the reference file's mtime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-anewer",
        .summary = "match when atime is newer than the reference file's mtime (== -neweram)",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-cnewer",
        .summary = "match when ctime is newer than the reference file's mtime (== -newercm)",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-neweraa",
        .summary = "match when atime is newer than the reference file's atime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newerac",
        .summary = "match when atime is newer than the reference file's ctime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-neweram",
        .summary = "match when atime is newer than the reference file's mtime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newerca",
        .summary = "match when ctime is newer than the reference file's atime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newercc",
        .summary = "match when ctime is newer than the reference file's ctime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newercm",
        .summary = "match when ctime is newer than the reference file's mtime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newerma",
        .summary = "match when mtime is newer than the reference file's atime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newermc",
        .summary = "match when mtime is newer than the reference file's ctime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newermm",
        .summary = "match when mtime is newer than the reference file's mtime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newerat",
        .summary = "match when atime is newer than a time string",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newerct",
        .summary = "match when ctime is newer than a time string",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newermt",
        .summary = "match when mtime is newer than a time string",
        .kind = Kind::kTest,
        .arity = 1,
    },
    // Birthtime -newerXY combos (BSD-compat): X or Y = B (birth/creation time).
    {
        .name = "-newerBa",
        .summary = "match when birth time is newer than the reference file's atime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newerBc",
        .summary = "match when birth time is newer than the reference file's ctime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newerBm",
        .summary = "match when birth time is newer than the reference file's mtime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newerBB",
        .summary = "match when birth time is newer than the reference file's birth time",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newerBt",
        .summary = "match when birth time is newer than a time string",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-neweraB",
        .summary = "match when atime is newer than the reference file's birth time",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newercB",
        .summary = "match when ctime is newer than the reference file's birth time",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-newermB",
        .summary = "match when mtime is newer than the reference file's birth time",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-mtime",
        .summary = "match the data-modification age in days",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-mmin",
        .summary = "match the data-modification age in minutes",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-atime",
        .summary = "match the access age in days",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-amin",
        .summary = "match the access age in minutes",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-ctime",
        .summary = "match the status-change age in days",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-cmin",
        .summary = "match the status-change age in minutes",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-Btime",
        .summary = "match the birth (creation) age in days",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-Bmin",
        .summary = "match the birth (creation) age in minutes",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-used",
        .summary = "match the whole days between atime and ctime",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-perm",
        .summary = "match the permission bits (octal or symbolic mode)",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-maxdepth",
        .summary = "descend at most N directory levels below each start",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-mindepth",
        .summary = "skip entries fewer than N levels below each start",
        .kind = Kind::kTest,
        .arity = 1,
    },
    {
        .name = "-depth",
        .summary = "process a directory's contents before the directory",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-d",
        .summary = "BSD/GNU short spelling of -depth",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-xdev",
        .summary = "do not descend into other filesystems",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-mount",
        .summary = "GNU/BSD synonym for -xdev",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-x",
        .summary = "BSD synonym for -xdev",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-daystart",
        .summary = "measure age tests from today's local midnight",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-ignore_readdir_race",
        .summary = "skip entries that vanish during the walk (ENOENT)",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-noignore_readdir_race",
        .summary = "report vanished entries as errors (default)",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-empty",
        .summary = "match an empty regular file or empty directory",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-sparse",
        .summary = "match a file with holes (allocated blocks < apparent size)",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-readable",
        .summary = "match entries the current user can read",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-writable",
        .summary = "match entries the current user can write",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-executable",
        .summary = "match entries the current user can execute",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-true",
        .summary = "always match",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-false",
        .summary = "never match",
        .kind = Kind::kTest,
        .arity = 0,
    },
    {
        .name = "-ls",
        .summary = "print an `ls -dils` style line per entry",
        .kind = Kind::kAction,
        .arity = 0,
    },
    {
        .name = "-print",
        .summary = "print the path followed by a newline",
        .kind = Kind::kAction,
        .arity = 0,
    },
    {
        .name = "-print0",
        .summary = "print the path followed by a NUL",
        .kind = Kind::kAction,
        .arity = 0,
    },
    {
        .name = "-printf",
        .summary = "print a custom format string",
        .kind = Kind::kAction,
        .arity = 1,
    },
    {
        // xff: -print with the OS line ending
        .name = "-println",
        .summary = "print the path with the OS line ending (xff)",
        .kind = Kind::kAction,
        .arity = 0,
        .style = Style::kXff,
    },
    {
        // xff: -printf + the OS line ending
        .name = "-printfln",
        .summary = "print a custom format with the OS line ending (xff)",
        .kind = Kind::kAction,
        .arity = 1,
        .style = Style::kXff,
    },
    {
        // xff: the line-output companion of the -rxc content predicate.
        .name = "-grep",
        .summary = "print each content line matching a regex, as path:line:text (xff)",
        .kind = Kind::kAction,
        .arity = 1,
        .style = Style::kXff,
        .cost = Cost::kExpensive,
    },
    {
        .name = "-fprint",
        .summary = "write -print output to a named file",
        .kind = Kind::kAction,
        .arity = 1,
    },
    {
        .name = "-fprint0",
        .summary = "write -print0 output to a named file",
        .kind = Kind::kAction,
        .arity = 1,
    },
    {
        .name = "-fprintf",
        .summary = "write -printf output to a named file",
        .kind = Kind::kAction,
        .arity = 2,
    },
    {
        .name = "-fls",
        .summary = "write -ls output to a named file",
        .kind = Kind::kAction,
        .arity = 1,
    },
    {
        .name = "-delete",
        .summary = "delete the matched entry",
        .kind = Kind::kAction,
        .arity = 0,
        .safety = Safety::kSafety,
    },
    {
        .name = "-prune",
        .summary = "do not descend into the matched directory",
        .kind = Kind::kAction,
        .arity = 0,
    },
    {
        .name = "-quit",
        .summary = "stop the search immediately",
        .kind = Kind::kAction,
        .arity = 0,
    },
    {
        .name = "-exec",
        .summary = "run a command per match (;) or batched (+)",
        .kind = Kind::kAction,
        .arity = -1,
        .safety = Safety::kSecurity,
    },
    {
        .name = "-execdir",
        .summary = "run a command in the matched entry's directory",
        .kind = Kind::kAction,
        .arity = -1,
        .safety = Safety::kSecurity,
    },
    {
        .name = "-ok",
        .summary = "like -exec, but prompt before each command",
        .kind = Kind::kAction,
        .arity = -1,
        .safety = Safety::kSecurity,
    },
    {
        .name = "-okdir",
        .summary = "like -execdir, but prompt before each command",
        .kind = Kind::kAction,
        .arity = -1,
        .safety = Safety::kSecurity,
    },
    {
        // -capture=NAME[=REGEX] cmd... ;
        .name = "-capture",
        .summary = "run a command and bind its output to {capture.NAME} (xff)",
        .kind = Kind::kAction,
        .arity = -1,
        .binding = Binding::kLabelRegex,
        .safety = Safety::kSecurity,
        .style = Style::kXff,
    },
    {
        // -capture run in the matched entry's directory
        .name = "-capturedir",
        .summary = "run -capture in the matched entry's directory (xff)",
        .kind = Kind::kAction,
        .arity = -1,
        .binding = Binding::kLabelRegex,
        .safety = Safety::kSecurity,
        .style = Style::kXff,
    },
    {
        .name = "-a",
        .summary = "logical AND (implicit between predicates)",
        .kind = Kind::kOperator,
        .arity = 0,
    },
    {
        .name = "-and",
        .summary = "logical AND (implicit between predicates)",
        .kind = Kind::kOperator,
        .arity = 0,
    },
    {
        .name = "-o",
        .summary = "logical OR",
        .kind = Kind::kOperator,
        .arity = 0,
    },
    {
        .name = "-or",
        .summary = "logical OR",
        .kind = Kind::kOperator,
        .arity = 0,
    },
    {
        .name = "-not",
        .summary = "logical negation",
        .kind = Kind::kOperator,
        .arity = 0,
    },
    {
        .name = "!",
        .summary = "logical negation",
        .kind = Kind::kOperator,
        .arity = 0,
    },
    // xff logical operators (no find has them): precedence NOT > AND/-nand > XOR/-xnor > OR/-nor.
    {
        .name = "-xor",
        .summary = "logical XOR; matches exactly one side (xff)",
        .kind = Kind::kOperator,
        .arity = 0,
        .style = Style::kXff,
    },
    {
        .name = "-nand",
        .summary = "logical NAND; ! (lhs -a rhs) (xff)",
        .kind = Kind::kOperator,
        .arity = 0,
        .style = Style::kXff,
    },
    {
        .name = "-nor",
        .summary = "logical NOR; ! (lhs -o rhs) (xff)",
        .kind = Kind::kOperator,
        .arity = 0,
        .style = Style::kXff,
    },
    {
        .name = "-xnor",
        .summary = "logical XNOR; matches when both sides agree (xff)",
        .kind = Kind::kOperator,
        .arity = 0,
        .style = Style::kXff,
    },
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

absl::Span<const Descriptor> All() {
  return kDescriptors;
}

}  // namespace xff::registry
