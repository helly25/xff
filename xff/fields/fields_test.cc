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

// setenv()/unsetenv() are POSIX, hidden by glibc under strict -std=c++23; request
// them explicitly for the {env.NAME} test. No effect on macOS.
#if defined(__linux__) && !defined(_GNU_SOURCE)
# define _GNU_SOURCE 1
#endif

#include "xff/fields/fields.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/vfs/entry.h"

namespace xff::fields {
namespace {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::UnorderedElementsAreArray;

struct FieldsTest : ::testing::Test {
  static vfs::Metadata Meta(vfs::FileType type, std::uint64_t size) {
    vfs::Metadata md;
    md.type = type;
    md.size = size;
    return md;
  }
};

TEST_F(FieldsTest, SubstitutesPathComponentsAndMetadata) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 12);
  EXPECT_THAT(
      Render("{name} in {dir} [{ext}] {size}b {type}", "a/b/file.tar.gz", md, 2), "file.tar.gz in a/b [gz] 12b f");
  EXPECT_THAT(Render("{stem}", "a/b/file.tar.gz", md, 2), "file.tar");
}

TEST_F(FieldsTest, MimeFieldRendersMediaTypeByExtension) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  EXPECT_THAT(Render("{mime}", "a/b/notes.txt", md, 0), "text/plain");
  EXPECT_THAT(Render("{mime}", "a/b/readme.md", md, 0), "text/markdown");
  EXPECT_THAT(Render("{mime}", "a/b/README", md, 0), "application/octet-stream");  // no extension
}

TEST_F(FieldsTest, OwnerIsAnAliasOfUser) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  // {owner} is an alias of {user}: both render the same owner name (the value is runtime-dependent).
  EXPECT_THAT(Render("{owner}", "f", md, 0), Render("{user}", "f", md, 0));
}

TEST_F(FieldsTest, BlocksFieldRendersAllocatedSpace) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 1);         // 1 apparent byte
  md.blocks = 16;                                              // 16 * 512 = 8 KiB allocated
  EXPECT_THAT(Render("{blocks}", "f", md, 0), "16");           // allocated 512-blocks (find's %b)
  EXPECT_THAT(Render("{blocks:h}", "f", md, 0), "8.0K");       // human-readable allocated bytes
  EXPECT_THAT(Render("{size} {blocks}", "f", md, 0), "1 16");  // apparent vs allocated differ
}

TEST_F(FieldsTest, DirOfTopLevelEntryIsDot) {
  EXPECT_THAT(Render("{dir}", "file", Meta(vfs::FileType::kRegular, 0), 0), ".");
}

TEST_F(FieldsTest, RelpathIsThePathRelativeToTheSearchRoot) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const Template compiled = Template::Compile("{relpath}");
  // Descendant: the root prefix and its separator are stripped (find's %P).
  EXPECT_THAT(compiled.Render(RenderContext{.path = "A/sub/x.txt", .root = "A", .metadata = md}), "sub/x.txt");
  // The root operand itself renders empty.
  EXPECT_THAT(compiled.Render(RenderContext{.path = "A", .root = "A", .metadata = md}), "");
  // No root recorded: best-effort whole path.
  EXPECT_THAT(compiled.Render(RenderContext{.path = "A/x", .metadata = md}), "A/x");
}

TEST_F(FieldsTest, DoubledBracesAreLiteral) {
  EXPECT_THAT(Render("{{x}}={name}", "p/q", Meta(vfs::FileType::kRegular, 0), 0), "{x}=q");
}

TEST_F(FieldsTest, UnknownFieldRendersEmpty) {
  EXPECT_THAT(Render("[{bogus}]", "p", Meta(vfs::FileType::kRegular, 0), 0), "[]");
}

TEST_F(FieldsTest, LineAndTextRenderTheGrepMatchLine) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const Template compiled = Template::Compile("{path}:{line}:{text}");
  EXPECT_THAT(
      compiled.Render(RenderContext{.path = "src/a.py", .metadata = md, .line_number = 12, .line_text = "  # TODO"}),
      "src/a.py:12:  # TODO");
}

TEST_F(FieldsTest, LineAndTextAreEmptyWithoutAMatchLine) {
  // line_number unset (outside a -grep line): {line}/{text} render empty so they
  // no-op in --template / -printf.
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const Template compiled = Template::Compile("[{line}][{text}]");
  EXPECT_THAT(compiled.Render(RenderContext{.path = "f", .metadata = md}), "[][]");
}

TEST_F(FieldsTest, MatchAndColumnRenderTheMatchedSpan) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const Template compiled = Template::Compile("{column}:{match}");
  EXPECT_THAT(
      compiled.Render(RenderContext{.path = "f", .metadata = md, .match_text = "E42", .match_column = 6}), "6:E42");
}

TEST_F(FieldsTest, MatchAndColumnAreEmptyWithoutASpan) {
  // match_column unset: {match}/{column} render empty (they only fire for -grep -o).
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const Template compiled = Template::Compile("[{column}][{match}]");
  EXPECT_THAT(compiled.Render(RenderContext{.path = "f", .metadata = md}), "[][]");
}

TEST_F(FieldsTest, HashFieldDigestsFileContent) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 3);
  const std::string path = std::string(::testing::TempDir()) + "/xff_fields_hash_abc";
  { std::ofstream(path) << "abc"; }
  // Bare {hash} is sha256 hex; the qualifier picks the algorithm and (after /) the encoding.
  EXPECT_THAT(
      Template::Compile("{hash}").Render(RenderContext{.path = path, .metadata = md}),
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_THAT(
      Template::Compile("{hash:md5}").Render(RenderContext{.path = path, .metadata = md}),
      "900150983cd24fb0d6963f7d28e17f72");
  EXPECT_THAT(
      Template::Compile("{hash:sha256/base64}").Render(RenderContext{.path = path, .metadata = md}),
      "ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=");
  // An unknown algorithm or encoding renders empty (the field convention).
  EXPECT_THAT(Template::Compile("[{hash:crc32}]").Render(RenderContext{.path = path, .metadata = md}), "[]");
  EXPECT_THAT(Template::Compile("[{hash:sha256/b64}]").Render(RenderContext{.path = path, .metadata = md}), "[]");
  std::remove(path.c_str());
}

TEST_F(FieldsTest, HashFieldOfUnreadableFileIsEmpty) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::string absent = std::string(::testing::TempDir()) + "/xff_fields_hash_absent";
  EXPECT_THAT(Template::Compile("[{hash}]").Render(RenderContext{.path = absent, .metadata = md}), "[]");
}

TEST_F(FieldsTest, LinesFieldCountsTextLines) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::string path = std::string(::testing::TempDir()) + "/xff_fields_lines";
  { std::ofstream(path) << "one\ntwo\nthree\n"; }
  EXPECT_THAT(Template::Compile("{lines}").Render(RenderContext{.path = path, .metadata = md}), "3");
  { std::ofstream(path) << "no trailing newline"; }  // a final unterminated line still counts
  EXPECT_THAT(Template::Compile("{lines}").Render(RenderContext{.path = path, .metadata = md}), "1");
  { std::ofstream(path) << ""; }  // truncate: an empty file is zero lines
  EXPECT_THAT(Template::Compile("{lines}").Render(RenderContext{.path = path, .metadata = md}), "0");
  std::remove(path.c_str());
}

TEST_F(FieldsTest, LinesFieldIsEmptyForBinaryUnreadableOrNonRegular) {
  const vfs::Metadata reg = Meta(vfs::FileType::kRegular, 0);
  const std::string path = std::string(::testing::TempDir()) + "/xff_fields_lines_bin";
  { std::ofstream(path, std::ios::binary).write("a\0b\n", 4); }  // a NUL byte in the content
  // A NUL byte marks the file binary -> empty, like the content detector.
  EXPECT_THAT(Template::Compile("[{lines}]").Render(RenderContext{.path = path, .metadata = reg}), "[]");
  // A regular file that cannot be read -> empty.
  const std::string absent = std::string(::testing::TempDir()) + "/xff_fields_lines_absent";
  EXPECT_THAT(Template::Compile("[{lines}]").Render(RenderContext{.path = absent, .metadata = reg}), "[]");
  // A non-regular entry is never counted, even at a readable path.
  const vfs::Metadata dir = Meta(vfs::FileType::kDirectory, 0);
  EXPECT_THAT(Template::Compile("[{lines}]").Render(RenderContext{.path = path, .metadata = dir}), "[]");
  std::remove(path.c_str());
}

TEST_F(FieldsTest, TimeFieldQualifiers) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  md.mtime = absl::FromUnixSeconds(1'700'000'000);  // 2023-11-14, mid-month: the year is timezone-stable
  EXPECT_THAT(Render("{mtime:epoch}", "f", md, 0), "1700000000");
  EXPECT_THAT(Render("{mtime:%Y}", "f", md, 0), "2023");
  EXPECT_THAT(Render("{mtime:iso}", "f", md, 0), HasSubstr("2023"));
  EXPECT_THAT(Render("{mtime}", "f", md, 0), HasSubstr("2023"));  // default ISO-8601
}

TEST_F(FieldsTest, TimeFieldsRenderInTheContextZone) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  md.mtime = absl::FromUnixSeconds(1'600'000'000);  // 2020-09-13 12:26:40 UTC
  const Template compiled = Template::Compile("{mtime:%z}|{mtime:%H}");
  // UTC: zero offset, hour 12 (RenderContext::tz drives the formatting zone).
  EXPECT_THAT(
      compiled.Render(RenderContext{.path = "f", .metadata = md, .depth = 0, .tz = absl::UTCTimeZone()}), "+0000|12");
  // UTC+1 (fixed): +0100 offset, hour 13 -- the same instant, a different zone.
  EXPECT_THAT(
      compiled.Render(RenderContext{.path = "f", .metadata = md, .depth = 0, .tz = absl::FixedTimeZone(3'600)}),
      "+0100|13");
}

TEST_F(FieldsTest, BareTimeFieldUsesTheTimeFormatDefault) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  md.mtime = absl::FromUnixSeconds(1'600'000'000);  // 2020-09-13 12:26:40 UTC
  const Template bare = Template::Compile("{mtime}");
  // A bare {mtime} (no qualifier) renders with the --time-format default...
  EXPECT_THAT(
      bare.Render(RenderContext{.path = "f", .metadata = md, .tz = absl::UTCTimeZone(), .time_format = "epoch"}),
      "1600000000");
  // ...with no --time-format default it falls back to the "space" form...
  EXPECT_THAT(
      bare.Render(RenderContext{.path = "f", .metadata = md, .tz = absl::UTCTimeZone()}), "2020-09-13 12:26:40 +0000");
  // ...and an explicit {mtime:QUAL} qualifier always wins over the default.
  const Template qualified = Template::Compile("{mtime:%Y}");
  EXPECT_THAT(
      qualified.Render(RenderContext{.path = "f", .metadata = md, .tz = absl::UTCTimeZone(), .time_format = "epoch"}),
      "2020");
}

TEST_F(FieldsTest, ModeAndOwnerFields) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  md.mode = 0644;
  md.uid = 1'234'567;  // unlikely to have a passwd/group entry -> numeric fallback (find's %u/%g)
  md.gid = 1'234'567;
  md.dev = 42;
  EXPECT_THAT(Render("{mode} {user}:{group}", "f", md, 0), "644 1234567:1234567");
  EXPECT_THAT(Render("{uid}:{gid} dev={dev}", "f", md, 0), "1234567:1234567 dev=42");  // numeric ids + device
}

TEST_F(FieldsTest, AccessRendersTheSymbolicModeString) {
  vfs::Metadata reg = Meta(vfs::FileType::kRegular, 0);
  reg.mode = 0644;
  EXPECT_THAT(Render("{access}", "f", reg, 0), "-rw-r--r--");  // ls -l / stat %A style
  vfs::Metadata dir = Meta(vfs::FileType::kDirectory, 0);
  dir.mode = 0755;
  EXPECT_THAT(Render("{access}", "d", dir, 0), "drwxr-xr-x");
  vfs::Metadata suid = Meta(vfs::FileType::kRegular, 0);
  suid.mode = 04755;  // setuid + rwxr-xr-x -> owner exec shows 's'
  EXPECT_THAT(Render("{access}", "f", suid, 0), "-rwsr-xr-x");
  vfs::Metadata sticky = Meta(vfs::FileType::kDirectory, 0);
  sticky.mode = 01777;  // sticky + rwxrwxrwx -> other exec shows 't' (e.g. /tmp)
  EXPECT_THAT(Render("{access}", "d", sticky, 0), "drwxrwxrwt");
}

TEST_F(FieldsTest, HumanSizeAndSuffixes) {
  EXPECT_THAT(Render("{size:h}", "f", Meta(vfs::FileType::kRegular, 1'536), 0), "1.5K");
  EXPECT_THAT(Render("{size:h}", "f", Meta(vfs::FileType::kRegular, 500), 0), "500");   // < 1 KiB: plain
  EXPECT_THAT(Render("{size}", "f", Meta(vfs::FileType::kRegular, 1'536), 0), "1536");  // no qualifier
  EXPECT_THAT(Render("{suffixes}", "a/b/file.tar.gz", Meta(vfs::FileType::kRegular, 0), 0), ".tar.gz");
  EXPECT_THAT(Render("{suffixes}", "a/b/file", Meta(vfs::FileType::kRegular, 0), 0), "");
  // {suffix} is the last extension WITH its dot; {ext} drops the dot; {suffixes} is all.
  EXPECT_THAT(Render("{suffix}|{ext}", "a/b/file.tar.gz", Meta(vfs::FileType::kRegular, 0), 0), ".gz|gz");
  EXPECT_THAT(Render("[{suffix}]", "a/b/file", Meta(vfs::FileType::kRegular, 0), 0), "[]");
}

TEST_F(FieldsTest, CoreStripsAllExtensions) {
  // {core} = the filename with ALL extensions removed; complement of {suffixes}.
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  EXPECT_THAT(Render("{core}", "a/b/foo.tar.gz", md, 0), "foo");                     // vs {stem}="foo.tar"
  EXPECT_THAT(Render("{core}|{suffixes}", "a/b/foo.tar.gz", md, 0), "foo|.tar.gz");  // core + suffixes == name
  EXPECT_THAT(Render("{core}", "a/b/foo", md, 0), "foo");                            // no extension
  EXPECT_THAT(Render("{core}", "a/b/.bashrc", md, 0), ".bashrc");  // a leading dot is not an extension
}

TEST_F(FieldsTest, PathComponentQualifierDecomposesAnyPathValuedField) {
  // A component qualifier post-processes, treating the field's value as a path -- so
  // {path:X} mirrors the flat {X} field, and it composes onto {relpath}, {def.NAME}, etc.
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::map<std::string, std::string> defs = {{"B", "/other/tree/report.tar.gz"}};
  const Template compiled = Template::Compile("{path:name}|{path:stem}|{path:core}|{path:ext}|{path:dir}");
  EXPECT_THAT(
      compiled.Render(RenderContext{.path = "a/b/foo.tar.gz", .metadata = md}), "foo.tar.gz|foo.tar|foo|gz|a/b");
  // {path:name} equals the flat {name}.
  EXPECT_THAT(Render("{path:name}", "a/b/foo.tar.gz", md, 0), Render("{name}", "a/b/foo.tar.gz", md, 0));
  // Composes on {relpath} (root-relative) and on a {def.NAME} value.
  EXPECT_THAT(
      Template::Compile("{relpath:dir}").Render(RenderContext{.path = "A/sub/x.cc", .root = "A", .metadata = md}),
      "sub");
  EXPECT_THAT(
      Template::Compile("{def.B:core}").Render(RenderContext{.path = "f", .metadata = md, .defines = &defs}), "report");
}

TEST_F(FieldsTest, TargetRendersTheSymlinkTargetAndComposes) {
  const vfs::Metadata link = Meta(vfs::FileType::kSymlink, 0);
  const Template compiled = Template::Compile("{target}|{target:name}|{target:core}");
  EXPECT_THAT(
      compiled.Render(RenderContext{.path = "l", .link_target = "sub/report.tar.gz", .metadata = link}),
      "sub/report.tar.gz|report.tar.gz|report");
  // Empty for a non-symlink (link_target unset by the driver).
  EXPECT_THAT(Render("[{target}]", "f", Meta(vfs::FileType::kRegular, 0), 0), "[]");
}

TEST_F(FieldsTest, QuotedQualifierCarriesBracesColonsAndQuotes) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  md.mtime = absl::FromUnixSeconds(1'700'000'000);  // 2023-11-14, mid-month: the year is timezone-stable
  // Escaped quotes/braces survive into the strftime format; the inner '}' does not end the field.
  EXPECT_THAT(Render(R"({mtime:"{\"y\":\"%Y\"}"})", "f", md, 0), R"({"y":"2023"})");
  // A quoted qualifier is dequoted before matching, so {size:"h"} still selects human-readable size.
  EXPECT_THAT(Render(R"({size:"h"})", "f", Meta(vfs::FileType::kRegular, 1'536), 0), "1.5K");
  // An unterminated quoted qualifier leaves the '{' and the remaining text literal.
  EXPECT_THAT(Render(R"({mtime:"%Y)", "f", md, 0), R"({mtime:"%Y)");
}

TEST_F(FieldsTest, CompiledTemplateRendersManyEntries) {
  const Template compiled = Template::Compile("{name}={size:h}");  // parsed once, reused below
  const vfs::Metadata small = Meta(vfs::FileType::kRegular, 1);
  const vfs::Metadata big = Meta(vfs::FileType::kRegular, 1'536);
  EXPECT_THAT(compiled.Render(RenderContext{.path = "a/x", .metadata = small, .depth = 0}), "x=1");
  EXPECT_THAT(compiled.Render(RenderContext{.path = "b/big", .metadata = big, .depth = 0}), "big=1.5K");
}

TEST_F(FieldsTest, FieldNameAliasesResolveToTheSameRenderer) {
  vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  md.mode = 0700;
  EXPECT_THAT(Render("{file}|{name}", "a/b.txt", md, 0), "b.txt|b.txt");
  EXPECT_THAT(Render("{extension}|{ext}", "a/b.txt", md, 0), "txt|txt");
  EXPECT_THAT(Render("{perm}|{mode}", "a/b.txt", md, 0), "700|700");
}

TEST_F(FieldsTest, RootFieldReportsTheSearchRoot) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const Template compiled = Template::Compile("{root}|{path}");
  EXPECT_THAT(compiled.Render(RenderContext{.path = "r/sub/f", .root = "r", .metadata = md, .depth = 2}), "r|r/sub/f");
  // An empty root (e.g. via the rootless convenience overload) renders empty.
  EXPECT_THAT(compiled.Render(RenderContext{.path = "x", .metadata = md, .depth = 0}), "|x");
}

TEST_F(FieldsTest, EmptyPlaceholderIsPathAndContextOverloadResolvesRoot) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  // {} is find's full-path placeholder -> an alias for {path}.
  EXPECT_THAT(Render("{}", "a/b/c.txt", md, 0), "a/b/c.txt");
  EXPECT_THAT(Render("echo {}", "p", md, 0), "echo p");
  // The context overload resolves {root}, which the rootless 4-arg overload cannot.
  EXPECT_THAT(
      Render("{root}:{}", RenderContext{.path = "r/sub/f", .root = "r", .metadata = md, .depth = 1}), "r:r/sub/f");
}

TEST_F(FieldsTest, NumericPlaceholdersRenderRegexCaptures) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::vector<std::string> captures = {"a/b/c.txt", "a/b", "c", "txt"};  // [0]=whole match, 1..3 groups
  const RenderContext ctx{.path = "a/b/c.txt", .metadata = md, .captures = &captures};
  EXPECT_THAT(Render("{1}-{3}", ctx), "a/b-txt");
  EXPECT_THAT(Render("{0}", ctx), "a/b/c.txt");            // {0} is the whole match
  EXPECT_THAT(Render("{9}", ctx), "");                     // out of range -> empty
  EXPECT_THAT(Render("[{1}]", "a/b/c.txt", md, 0), "[]");  // no captures available -> empty
}

TEST_F(FieldsTest, EnvNamespaceReadsEnvironment) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  ::setenv("XFF_TEST_ENV_VAR", "hello", 1);
  EXPECT_THAT(Render("{env.XFF_TEST_ENV_VAR}", "p", md, 0), "hello");
  ::unsetenv("XFF_TEST_ENV_VAR");
  EXPECT_THAT(Render("{env.XFF_TEST_ENV_VAR}", "p", md, 0), "");  // unset -> empty
}

TEST_F(FieldsTest, DefNamespaceReadsDefines) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::map<std::string, std::string> defines = {{"greeting", "hi"}, {"n", "42"}};
  const RenderContext ctx{.path = "p", .metadata = md, .defines = &defines};
  EXPECT_THAT(Render("{def.greeting}-{def.n}", ctx), "hi-42");
  EXPECT_THAT(Render("{def.missing}", ctx), "");              // undefined -> empty
  EXPECT_THAT(Render("[{def.greeting}]", "p", md, 0), "[]");  // no defines map -> empty
}

TEST_F(FieldsTest, OutputNamespaceReadsCaptureResults) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::map<std::string, std::string> outputs = {{"lines", "42"}};
  const RenderContext ctx{.path = "p", .metadata = md, .outputs = &outputs};
  EXPECT_THAT(Render("{capture.lines}", ctx), "42");
  EXPECT_THAT(Render("[{capture.lines}]", ctx), "[42]");       // value composes with surrounding literals
  EXPECT_THAT(Render("{capture.missing}", ctx), "");           // unset -> empty
  EXPECT_THAT(Render("[{capture.lines}]", "p", md, 0), "[]");  // no outputs map -> empty
}

TEST_F(FieldsTest, RewriteQualifierTransformsTheValue) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  EXPECT_THAT(Render("{name:s/\\.txt$/.md/}", "a/b/c.txt", md, 0), "c.md");               // first match
  EXPECT_THAT(Render("{name:s/[aeiou]//g}", "a/b/foo.txt", md, 0), "f.txt");              // g = all matches
  EXPECT_THAT(Render("{path:s#/#_#g}", "a/b/c", md, 0), "a_b_c");                         // alternate delimiter
  EXPECT_THAT(Render("{size:h}", "f", Meta(vfs::FileType::kRegular, 1'536), 0), "1.5K");  // non-rewrite intact
}

TEST_F(FieldsTest, MExtractorYieldsAPerLineValueStream) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  // git-blame --line-porcelain-shaped output: many lines, an `author X` header per source line.
  const std::map<std::string, std::string> outputs = {
      {"blame", "author Bob\nauthor-mail <b@x>\nauthor Ann\n\tsource line\nauthor Bob\n"}};
  const Template compiled = Template::Compile("{capture.blame:m/^author (.+)$/\\1/}");
  const RenderContext ctx{.path = "f", .metadata = md, .outputs = &outputs};
  // AsExtraction: one value per matching line, non-matching lines dropped, \1 = capture group.
  ASSERT_TRUE(compiled.AsExtraction(ctx).has_value());
  EXPECT_THAT(*compiled.AsExtraction(ctx), ElementsAre("Bob", "Ann", "Bob"));
  // Scalar Render projects the stream as the matches newline-joined.
  EXPECT_THAT(compiled.Render(ctx), "Bob\nAnn\nBob");
}

TEST_F(FieldsTest, MExtractorHonorsDelimiterFlagsAndWholeMatch) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::map<std::string, std::string> outputs = {{"x", "AUTHOR Bob\nnope\nauthor Ann"}};
  const RenderContext ctx{.path = "f", .metadata = md, .outputs = &outputs};
  // Alternate ',' delimiter + case-insensitive 'i' flag; \1 keeps the name.
  EXPECT_THAT(*Template::Compile("{capture.x:m,^author (.+)$,\\1,i}").AsExtraction(ctx), ElementsAre("Bob", "Ann"));
  // \0 keeps the whole matching line; a non-matching line is dropped.
  EXPECT_THAT(*Template::Compile("{capture.x:m/nope/\\0/}").AsExtraction(ctx), ElementsAre("nope"));
}

TEST_F(FieldsTest, RewriteChainAppliesCommandsInSequence) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  // A ;-separated s-chain applies each substitution left to right; a command after ; may omit the
  // leading s (it defaults to a substitution).
  EXPECT_THAT(Render("{name:s/o/0/g;s/txt/md/}", "a/foo.txt", md, 0), "f00.md");
  EXPECT_THAT(Render("{name:s/o/0/g;/txt/md/}", "a/foo.txt", md, 0), "f00.md");    // 2nd command letterless
  EXPECT_THAT(Render("{path:s#/#_#g;s/^/root_/}", "a/b/c", md, 0), "root_a_b_c");  // alt delimiter + chain
}

TEST_F(FieldsTest, MExtractorChainFiltersThenSubstitutes) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::map<std::string, std::string> outputs = {{"blame", "author Bob Smith\nother line\nauthor Ann Lee\n"}};
  // First command extracts the author (and filters non-author lines); the second normalizes the
  // extracted value (spaces -> underscores) per surviving line.
  const Template compiled = Template::Compile("{capture.blame:m/^author (.+)$/\\1/;s/ /_/g}");
  const RenderContext ctx{.path = "f", .metadata = md, .outputs = &outputs};
  ASSERT_TRUE(compiled.AsExtraction(ctx).has_value());
  EXPECT_THAT(*compiled.AsExtraction(ctx), ElementsAre("Bob_Smith", "Ann_Lee"));
}

TEST_F(FieldsTest, MExtractorEmptyStreamVersusNonExtractionTemplate) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  const std::map<std::string, std::string> outputs = {{"x", "aaa\nbbb"}};
  const RenderContext ctx{.path = "f", .metadata = md, .outputs = &outputs};
  // A well-formed m// that matches nothing is an empty stream (present, but no values).
  EXPECT_THAT(*Template::Compile("{capture.x:m/zzz/\\0/}").AsExtraction(ctx), IsEmpty());
  // Anything that is not exactly one m// field is not a value stream.
  EXPECT_THAT(Template::Compile("{name}").AsExtraction(ctx), Eq(std::nullopt));                  // scalar field
  EXPECT_THAT(Template::Compile("x {capture.x:m/./\\0/}").AsExtraction(ctx), Eq(std::nullopt));  // a literal present
}

// The --help=fields SOT guard: FieldDocs() must document exactly the renderable field
// names, so the help topic can never drift from the actual field table.
TEST_F(FieldsTest, FieldDocsCoverEveryRenderableField) {
  std::vector<std::string_view> documented;
  for (const FieldDoc& doc : FieldDocs()) {
    documented.push_back(doc.name);
    for (const std::string_view alias : doc.aliases) {
      documented.push_back(alias);
    }
  }
  std::vector<std::string_view> renderable;
  for (const std::string_view name : FieldNames()) {
    if (!name.empty()) {  // "" backs {} (an alias of {path}); asserted separately below
      renderable.push_back(name);
    }
  }
  EXPECT_THAT(documented, UnorderedElementsAreArray(renderable));
}

TEST_F(FieldsTest, EmptyNameIsThePathAlias) {
  const vfs::Metadata md = Meta(vfs::FileType::kRegular, 0);
  EXPECT_THAT(Render("{}", "a/b/c", md, 0), "a/b/c");                           // {} -> full path
  EXPECT_THAT(Render("{}", "a/b/c", md, 0), Render("{path}", "a/b/c", md, 0));  // same as {path}
}

TEST_F(FieldsTest, EveryFieldDocHasGroupHeaderAndSummary) {
  for (const FieldDoc& doc : FieldDocs()) {
    EXPECT_THAT(doc.group, Not(IsEmpty())) << "field {" << doc.name << "}";
    EXPECT_THAT(doc.header, Not(IsEmpty())) << "field {" << doc.name << "}";
    EXPECT_THAT(doc.summary, Not(IsEmpty())) << "field {" << doc.name << "}";
  }
}

TEST_F(FieldsTest, PathComponentKeywordsAreExposedForHelp) {
  EXPECT_THAT(PathComponentKeywords(), AllOf(Not(IsEmpty()), Contains("stem"), Contains("dir")));
}

TEST_F(FieldsTest, IsKnownFieldAcceptsVocabularyRejectsUnknown) {
  // Powers --columns validation: a builtin, a qualified name, a namespace, and a capture
  // index are known; a typo is not.
  EXPECT_TRUE(IsKnownField("path"));
  EXPECT_TRUE(IsKnownField("mtime:%Y"));  // a name with a :qualifier
  EXPECT_TRUE(IsKnownField("env.HOME"));  // a dynamic namespace
  EXPECT_TRUE(IsKnownField("def.B"));
  EXPECT_TRUE(IsKnownField("0"));  // a {0}..{N} capture index
  EXPECT_FALSE(IsKnownField("bogus"));
}

}  // namespace
}  // namespace xff::fields
