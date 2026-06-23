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
#include <map>
#include <string>

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/vfs/entry.h"

namespace xff::fields {
namespace {

using ::testing::HasSubstr;

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

TEST_F(FieldsTest, DirOfTopLevelEntryIsDot) {
  EXPECT_THAT(Render("{dir}", "file", Meta(vfs::FileType::kRegular, 0), 0), ".");
}

TEST_F(FieldsTest, DoubledBracesAreLiteral) {
  EXPECT_THAT(Render("{{x}}={name}", "p/q", Meta(vfs::FileType::kRegular, 0), 0), "{x}=q");
}

TEST_F(FieldsTest, UnknownFieldRendersEmpty) {
  EXPECT_THAT(Render("[{bogus}]", "p", Meta(vfs::FileType::kRegular, 0), 0), "[]");
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
  EXPECT_THAT(Render("{mode} {user}:{group}", "f", md, 0), "644 1234567:1234567");
}

TEST_F(FieldsTest, HumanSizeAndSuffixes) {
  EXPECT_THAT(Render("{size:h}", "f", Meta(vfs::FileType::kRegular, 1'536), 0), "1.5K");
  EXPECT_THAT(Render("{size:h}", "f", Meta(vfs::FileType::kRegular, 500), 0), "500");   // < 1 KiB: plain
  EXPECT_THAT(Render("{size}", "f", Meta(vfs::FileType::kRegular, 1'536), 0), "1536");  // no qualifier
  EXPECT_THAT(Render("{suffixes}", "a/b/file.tar.gz", Meta(vfs::FileType::kRegular, 0), 0), ".tar.gz");
  EXPECT_THAT(Render("{suffixes}", "a/b/file", Meta(vfs::FileType::kRegular, 0), 0), "");
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

}  // namespace
}  // namespace xff::fields
