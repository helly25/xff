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

#include "xff/engine/evaluate.h"

#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "absl/time/time.h"
#include "mbo/testing/status.h"
#include "xff/engine/walk.h"
#include "xff/parser/parser.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/local_fs.h"

namespace xff::engine {
namespace {

using ::mbo::testing::IsOk;
using ::testing::Eq;
using ::testing::IsEmpty;

struct EvaluateTest : ::testing::Test {
  // Parses `. <expr...>` and evaluates the expression against `visit`, capturing
  // any action output in `emitted_`.
  bool Match(const std::vector<std::string>& expr, const Visit& visit) {
    emitted_.clear();
    control_ = {};
    std::vector<std::string> argv;
    argv.reserve(expr.size() + 1);
    argv.emplace_back(".");
    argv.insert(argv.end(), expr.begin(), expr.end());
    const auto command = parser::Parse(argv);
    EXPECT_THAT(command, IsOk());
    if (!command.ok() || command->expression == nullptr) {
      return false;
    }
    const auto sink = [this](std::string_view record) { emitted_ += record; };
    captures_.clear();
    outputs_.clear();
    EvalContext context{
        .visit = visit, .emit = sink, .fs = fs_, .now = now_, .control = control_, .exec_fields = exec_fields_,
        .captures = exec_fields_ ? &captures_ : nullptr, .outputs = &outputs_};
    return Evaluate(*command->expression, context);
  }

  // A Visit of `type`, with `path`/`name` backed by the caller and metadata by
  // `storage` (both must outlive the returned Visit).
  static Visit MakeVisit(std::string_view path, std::string_view name, vfs::FileType type, vfs::Metadata& storage) {
    storage.type = type;
    return Visit{.path = path, .name = name, .depth = 1, .metadata = storage};
  }

  std::string emitted_;
  vfs::LocalFs fs_;
  // A fixed reference instant for age-test (-mtime/-mmin) cases; the entry's
  // mtime is set relative to this so the assertions are clock-independent.
  const absl::Time now_ = absl::FromUnixSeconds(1700000000);
  Control control_;  // set by Match from the most recent evaluation (-prune/-quit)
  bool exec_fields_ = false;  // when true, Match enables --exec-fields token substitution
  std::vector<std::string> captures_;  // -regex groups captured during the most recent (gated) Match
  std::map<std::string, std::string> outputs_;  // --capture results from the most recent Match
};

TEST_F(EvaluateTest, TrueAndFalse) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo", "foo", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-true"}, visit));
  EXPECT_FALSE(Match({"-false"}, visit));
}

TEST_F(EvaluateTest, NameGlobsBasename) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-name", "*.txt"}, visit));
  EXPECT_TRUE(Match({"-name", "foo.*"}, visit));
  EXPECT_FALSE(Match({"-name", "*.md"}, visit));
  EXPECT_FALSE(Match({"-name", "dir/*"}, visit)) << "-name matches the basename, not the path";
}

TEST_F(EvaluateTest, INameFoldsCase) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/Foo.TXT", "Foo.TXT", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-iname", "foo.txt"}, visit));
  EXPECT_FALSE(Match({"-name", "foo.txt"}, visit));
}

TEST_F(EvaluateTest, PathGlobsWholePath) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/c.txt", "c.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-path", "a/*/c.txt"}, visit));
  EXPECT_TRUE(Match({"-path", "*/c.txt"}, visit)) << "* spans slashes for -path";
  EXPECT_FALSE(Match({"-path", "c.txt"}, visit));
  EXPECT_TRUE(Match({"-ipath", "A/B/*"}, visit));
}

TEST_F(EvaluateTest, TypeMatchesFileType) {
  vfs::Metadata file_md;
  const Visit file = MakeVisit("x", "x", vfs::FileType::kRegular, file_md);
  vfs::Metadata dir_md;
  const Visit dir = MakeVisit("d", "d", vfs::FileType::kDirectory, dir_md);
  EXPECT_TRUE(Match({"-type", "f"}, file));
  EXPECT_FALSE(Match({"-type", "d"}, file));
  EXPECT_TRUE(Match({"-type", "d"}, dir));
  EXPECT_FALSE(Match({"-type", "f"}, dir));
}

TEST_F(EvaluateTest, AndOrNotShortCircuit) {
  vfs::Metadata md;
  const Visit txt = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-type", "f", "-name", "*.txt"}, txt));        // implicit -a
  EXPECT_FALSE(Match({"-type", "d", "-name", "*.txt"}, txt));       // -a, first fails
  EXPECT_TRUE(Match({"-type", "d", "-o", "-name", "*.txt"}, txt));  // -o
  EXPECT_TRUE(Match({"!", "-type", "d"}, txt));                     // -not
  EXPECT_FALSE(Match({"!", "-name", "*.txt"}, txt));
}

TEST_F(EvaluateTest, PrintActionsEmit) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-print"}, visit));
  EXPECT_THAT(emitted_, Eq("dir/foo.txt\n"));
  EXPECT_TRUE(Match({"-print0"}, visit));
  EXPECT_THAT(emitted_, Eq(std::string("dir/foo.txt\0", 12)));
}

TEST_F(EvaluateTest, ShortCircuitSkipsAction) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-type", "f", "-print"}, visit));
  EXPECT_THAT(emitted_, Eq("dir/foo.txt\n"));
  EXPECT_FALSE(Match({"-type", "d", "-print"}, visit)) << "-type d fails; -a short-circuits before -print";
  EXPECT_THAT(emitted_, IsEmpty());
}

TEST_F(EvaluateTest, SizeMatchesBytesAndUnits) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.size = 5;  // bytes
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-size", "5c"}, visit));
  EXPECT_FALSE(Match({"-size", "4c"}, visit));
  EXPECT_TRUE(Match({"-size", "+4c"}, visit));
  EXPECT_TRUE(Match({"-size", "-6c"}, visit));
  EXPECT_FALSE(Match({"-size", "+5c"}, visit));
  EXPECT_TRUE(Match({"-size", "1"}, visit)) << "5 bytes rounds up to one 512-byte block";
  EXPECT_TRUE(Match({"-size", "1k"}, visit)) << "5 bytes rounds up to one 1k unit";
}

TEST_F(EvaluateTest, PermMatchesOctalModes) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mode = 0644;  // rw-r--r--
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-perm", "644"}, visit));    // exact
  EXPECT_FALSE(Match({"-perm", "640"}, visit));
  EXPECT_TRUE(Match({"-perm", "-200"}, visit));   // -MODE: owner-write bit set
  EXPECT_TRUE(Match({"-perm", "-044"}, visit));   // group + other read set
  EXPECT_FALSE(Match({"-perm", "-022"}, visit));  // group/other write NOT set
  EXPECT_TRUE(Match({"-perm", "/040"}, visit));   // /MODE: any bit (group read) set
  EXPECT_FALSE(Match({"-perm", "/022"}, visit));  // none of group/other write set
}

TEST_F(EvaluateTest, EmptyMatchesZeroByteFilesNotOthers) {
  vfs::Metadata empty_file;
  empty_file.type = vfs::FileType::kRegular;
  empty_file.size = 0;
  EXPECT_TRUE(Match({"-empty"}, Visit{.path = "f", .name = "f", .depth = 1, .metadata = empty_file}));

  vfs::Metadata nonempty_file;
  nonempty_file.type = vfs::FileType::kRegular;
  nonempty_file.size = 5;
  EXPECT_FALSE(Match({"-empty"}, Visit{.path = "f", .name = "f", .depth = 1, .metadata = nonempty_file}));

  vfs::Metadata link;
  link.type = vfs::FileType::kSymlink;
  EXPECT_FALSE(Match({"-empty"}, Visit{.path = "l", .name = "l", .depth = 1, .metadata = link}))
      << "-empty matches only regular files and directories";
}

TEST_F(EvaluateTest, LinksMatchesHardLinkCount) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.nlink = 1;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-links", "1"}, visit));
  EXPECT_FALSE(Match({"-links", "2"}, visit));
  EXPECT_TRUE(Match({"-links", "-2"}, visit));
  EXPECT_TRUE(Match({"-links", "+0"}, visit));
  EXPECT_FALSE(Match({"-links", "+1"}, visit));
}

TEST_F(EvaluateTest, NewerFalseWhenReferenceMissing) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // The reference cannot be stat'd, so -newer is false (real comparisons are
  // covered by the conformance test against actual files).
  EXPECT_FALSE(Match({"-newer", "/no/such/reference/file"}, visit));
}

TEST_F(EvaluateTest, MTimeMatchesWholeDaysAgo) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mtime = now_ - absl::Hours(60);  // 2.5 days ago -> floor to 2 whole days
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-mtime", "2"}, visit));    // floor(2.5) == 2
  EXPECT_FALSE(Match({"-mtime", "3"}, visit));
  EXPECT_TRUE(Match({"-mtime", "+1"}, visit));   // strictly older than 1 day
  EXPECT_FALSE(Match({"-mtime", "+2"}, visit));  // not strictly older than 2
  EXPECT_TRUE(Match({"-mtime", "-3"}, visit));   // strictly younger than 3 days
}

TEST_F(EvaluateTest, MMinMatchesWholeMinutesAgo) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mtime = now_ - absl::Minutes(150);  // 150 minutes ago
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-mmin", "150"}, visit));
  EXPECT_TRUE(Match({"-mmin", "+100"}, visit));
  EXPECT_FALSE(Match({"-mmin", "+150"}, visit));
  EXPECT_TRUE(Match({"-mmin", "-200"}, visit));
}

TEST_F(EvaluateTest, UidAndGidMatchNumericOwner) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.uid = 501;
  md.gid = 20;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-uid", "501"}, visit));
  EXPECT_FALSE(Match({"-uid", "500"}, visit));
  EXPECT_TRUE(Match({"-uid", "+500"}, visit));  // uid strictly greater than 500
  EXPECT_TRUE(Match({"-gid", "20"}, visit));
  EXPECT_FALSE(Match({"-gid", "21"}, visit));
  EXPECT_TRUE(Match({"-gid", "-21"}, visit));   // gid strictly less than 21
}

TEST_F(EvaluateTest, AccessAndChangeTimeFamily) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.atime = now_ - absl::Hours(60);     // 2.5 days / 3600 minutes ago
  md.ctime = now_ - absl::Minutes(150);  // 2.5 hours / 150 minutes ago
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-atime", "2"}, visit));   // -atime/-amin read atime: floor(2.5 days) == 2
  EXPECT_TRUE(Match({"-atime", "+1"}, visit));
  EXPECT_TRUE(Match({"-amin", "3600"}, visit));
  EXPECT_TRUE(Match({"-amin", "+3000"}, visit));
  EXPECT_TRUE(Match({"-ctime", "0"}, visit));   // -ctime/-cmin read ctime: 2.5 hours floors to 0 days
  EXPECT_TRUE(Match({"-cmin", "150"}, visit));
  EXPECT_FALSE(Match({"-cmin", "+150"}, visit));
}

TEST_F(EvaluateTest, UserGroupNumericFallback) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.uid = 501;
  md.gid = 20;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // A token that is not a known user/group but is all-digits is taken as a
  // literal id (matching find); real-name resolution is covered by the
  // conformance test against the current user/group.
  EXPECT_TRUE(Match({"-user", "501"}, visit));
  EXPECT_FALSE(Match({"-user", "500"}, visit));
  EXPECT_TRUE(Match({"-group", "20"}, visit));
  EXPECT_FALSE(Match({"-group", "21"}, visit));
}

TEST_F(EvaluateTest, CommaEvaluatesBothValueIsRight) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // The comma list evaluates both operands; its value is the right operand's.
  EXPECT_FALSE(Match({"-true", ",", "-false"}, visit));
  EXPECT_TRUE(Match({"-false", ",", "-true"}, visit));
  // Side effects on both sides still occur: two -print actions emit two records.
  EXPECT_TRUE(Match({"-print", ",", "-print"}, visit));
  EXPECT_THAT(emitted_, Eq("f\nf\n"));
}

TEST_F(EvaluateTest, PruneAndQuitSetControl) {
  vfs::Metadata md;
  md.type = vfs::FileType::kDirectory;
  const Visit visit{.path = "d", .name = "d", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-prune"}, visit));
  EXPECT_TRUE(control_.prune);
  EXPECT_FALSE(control_.quit);
  EXPECT_TRUE(Match({"-quit"}, visit));
  EXPECT_TRUE(control_.quit);
  EXPECT_FALSE(control_.prune);  // control is reset per Match
  // A short-circuited -prune does not fire: `-type f -prune` on a directory.
  EXPECT_FALSE(Match({"-type", "f", "-prune"}, visit));
  EXPECT_FALSE(control_.prune);
}

TEST_F(EvaluateTest, RegexMatchesWholePath) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/c.txt", "c.txt", vfs::FileType::kRegular, md);
  // -regex matches the whole path (not just the basename); -iregex folds case.
  EXPECT_TRUE(Match({"-regex", ".*\\.txt"}, visit));
  EXPECT_FALSE(Match({"-regex", ".*\\.md"}, visit));
  EXPECT_FALSE(Match({"-regex", "c\\.txt"}, visit));  // must match the whole path, not the basename
  EXPECT_TRUE(Match({"-iregex", ".*\\.TXT"}, visit));
}

TEST_F(EvaluateTest, NewerXYFalseWhenReferenceMissing) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("f", "f", vfs::FileType::kRegular, md);
  // -newerXY with an unreadable reference is false; real field comparisons are
  // covered by the conformance test against actual files.
  EXPECT_FALSE(Match({"-newermm", "/no/such/reference"}, visit));
  EXPECT_FALSE(Match({"-newerac", "/no/such/reference"}, visit));
}

TEST_F(EvaluateTest, PrintfExpandsDirectivesAndEscapes) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.size = 42;
  md.mode = 0644;
  md.nlink = 3;
  const Visit visit{.path = "a/b/c.txt", .name = "c.txt", .depth = 2, .metadata = md};
  EXPECT_TRUE(Match({"-printf", "%p|%f|%h|%s|%m|%d|%y\\n"}, visit));
  EXPECT_THAT(emitted_, Eq("a/b/c.txt|c.txt|a/b|42|644|2|f\n"));
  EXPECT_TRUE(Match({"-printf", "%%\\t%n"}, visit));  // literal %, tab escape, link count
  EXPECT_THAT(emitted_, Eq("%\t3"));
}

TEST_F(EvaluateTest, PrintlnAndPrintflnAppendOsLineEnding) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.size = 42;
  const Visit visit{.path = "a/b/c.txt", .name = "c.txt", .depth = 2, .metadata = md};
  // -println: -print with the OS line ending (LF on this platform).
  EXPECT_TRUE(Match({"-println"}, visit));
  EXPECT_THAT(emitted_, Eq("a/b/c.txt\n"));
  // -printfln: -printf plus a trailing OS line ending the format need not carry.
  EXPECT_TRUE(Match({"-printfln", "%f|%s"}, visit));
  EXPECT_THAT(emitted_, Eq("c.txt|42\n"));
}

TEST_F(EvaluateTest, ExecFieldsGatesNamedPlaceholderSubstitution) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/f.txt", "f.txt", vfs::FileType::kRegular, md);
  // Default (find-exact): {name} is not a placeholder, so the child compares the
  // literal "{name}" against "f.txt" -> not equal -> child exits 1 -> false.
  exec_fields_ = false;
  EXPECT_FALSE(Match({"-exec", "/bin/sh", "-c", "test \"{name}\" = f.txt", ";"}, visit));
  // With --exec-fields, {name} renders the basename -> equal -> child exits 0 -> true.
  exec_fields_ = true;
  EXPECT_TRUE(Match({"-exec", "/bin/sh", "-c", "test \"{name}\" = f.txt", ";"}, visit));
}

TEST_F(EvaluateTest, ExecFieldsSubstitutesRegexCaptures) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/c.txt", "c.txt", vfs::FileType::kRegular, md);
  // A preceding -regex match records its groups; -exec {1} then references group 1
  // ("a/b"). The child compares it to a/b -> exit 0 -> true.
  exec_fields_ = true;
  EXPECT_TRUE(
      Match({"-regex", "(.*)/([^/]+)\\.(.*)", "-exec", "/bin/sh", "-c", "test \"{1}\" = a/b", ";"}, visit));
  // Without the gate there is no capture and {1} stays literal -> not equal -> false.
  exec_fields_ = false;
  EXPECT_FALSE(
      Match({"-regex", "(.*)/([^/]+)\\.(.*)", "-exec", "/bin/sh", "-c", "test \"{1}\" = a/b", ";"}, visit));
}

TEST_F(EvaluateTest, CapturesAreVisibleLeftToRightOnly) {
  // The variable store accumulates left-to-right, so a binding (here a -regex
  // match) is in scope for later actions and only later -- the guarantee that
  // capture/-exec chaining rests on.
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/c.txt", "c.txt", vfs::FileType::kRegular, md);
  exec_fields_ = true;
  // -regex to the LEFT of -exec: its groups are bound when -exec runs, so {1} == "a/b".
  EXPECT_TRUE(
      Match({"-regex", "(.*)/([^/]+)\\.(.*)", "-exec", "/bin/sh", "-c", "test \"{1}\" = a/b", ";"}, visit));
  // -regex to the RIGHT: not yet evaluated when -exec runs, so {1} is still empty there.
  EXPECT_TRUE(
      Match({"-exec", "/bin/sh", "-c", "test -z \"{1}\"", ";", "-regex", "(.*)/([^/]+)\\.(.*)"}, visit));
}

TEST_F(EvaluateTest, CaptureBindsOutputNamespace) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/c.txt", "c.txt", vfs::FileType::kRegular, md);
  exec_fields_ = true;  // so the -exec reading {capture.tag} renders the vocabulary
  // --capture runs the command and binds {capture.tag}; the later -exec reads it.
  EXPECT_TRUE(Match(
      {"--capture=tag", "/bin/sh", "-c", "printf hi", ";", "-exec", "/bin/sh", "-c", "test \"{capture.tag}\" = hi",
       ";"},
      visit));
}

}  // namespace
}  // namespace xff::engine
