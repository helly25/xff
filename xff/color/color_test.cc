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

#include "xff/color/color.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/vfs/entry.h"

namespace xff::color {
namespace {

using ::testing::Eq;
using ::testing::IsEmpty;

struct ColorTest : ::testing::Test {};

TEST_F(ColorTest, ResolveWhenLastOccurrenceWins) {
  EXPECT_THAT(ResolveWhen({}), Eq(When::kAuto));  // default
  EXPECT_THAT(ResolveWhen({"--color"}), Eq(When::kAlways));
  EXPECT_THAT(ResolveWhen({"--color=always"}), Eq(When::kAlways));
  EXPECT_THAT(ResolveWhen({"--color=auto"}), Eq(When::kAuto));
  EXPECT_THAT(ResolveWhen({"--color=never"}), Eq(When::kNever));
  EXPECT_THAT(ResolveWhen({"--color=always", "--color=never"}), Eq(When::kNever));
}

TEST_F(ColorTest, EnabledCombinesModeTtyAndNoColor) {
  EXPECT_TRUE(Enabled(When::kAlways, /*stdout_is_tty=*/false, /*no_color_env=*/true));  // explicit wins over NO_COLOR
  EXPECT_FALSE(Enabled(When::kNever, /*stdout_is_tty=*/true, /*no_color_env=*/false));
  EXPECT_TRUE(Enabled(When::kAuto, /*stdout_is_tty=*/true, /*no_color_env=*/false));
  EXPECT_FALSE(Enabled(When::kAuto, /*stdout_is_tty=*/false, /*no_color_env=*/false));  // not a terminal
  EXPECT_FALSE(Enabled(When::kAuto, /*stdout_is_tty=*/true, /*no_color_env=*/true));    // NO_COLOR set
}

TEST_F(ColorTest, CodeForTypeUsesLsLikeScheme) {
  EXPECT_THAT(CodeForType(vfs::FileType::kDirectory, 0755), Eq("1;34"));
  EXPECT_THAT(CodeForType(vfs::FileType::kSymlink, 0777), Eq("1;36"));
  EXPECT_THAT(CodeForType(vfs::FileType::kFifo, 0644), Eq("33"));
  EXPECT_THAT(CodeForType(vfs::FileType::kRegular, 0755), Eq("1;32"));  // executable regular file
  EXPECT_THAT(CodeForType(vfs::FileType::kRegular, 0644), IsEmpty());   // plain file: no color
}

TEST_F(ColorTest, TheSchemeDefaultsToLsAndTheFlagPicks) {
  // Default kLs, because the colours a user expects are the ones their terminal is already themed
  // with; `xff` is the way back to the built-in scheme.
  EXPECT_THAT(ResolveScheme({}), Scheme::kLs);
  EXPECT_THAT(ResolveScheme({"--color-scheme=xff"}), Scheme::kXff);
  EXPECT_THAT(ResolveScheme({"--color-scheme=ls"}), Scheme::kLs);
  EXPECT_THAT(ResolveScheme({"--color-scheme=xff", "--color-scheme=ls"}), Scheme::kLs);  // last wins
  EXPECT_THAT(ResolveScheme({"--color-scheme=nonsense"}), Scheme::kLs);                  // unknown leaves the default
}

TEST_F(ColorTest, ADefaultPaletteIsTheBuiltInScheme) {
  const Palette palette;
  EXPECT_THAT(palette.CodeFor("dir", vfs::FileType::kDirectory, 0755), CodeForType(vfs::FileType::kDirectory, 0755));
  EXPECT_THAT(palette.CodeFor("a.txt", vfs::FileType::kRegular, 0644), IsEmpty());
}

TEST_F(ColorTest, LsColorsOverridesTheTypeItNames) {
  const Palette palette = Palette::FromLsColors("di=01;35:ln=04;36");
  EXPECT_THAT(palette.CodeFor("dir", vfs::FileType::kDirectory, 0755), "01;35");
  EXPECT_THAT(palette.CodeFor("link", vfs::FileType::kSymlink, 0777), "04;36");
  // A type $LS_COLORS says nothing about keeps the built-in colour rather than losing it.
  EXPECT_THAT(palette.CodeFor("pipe", vfs::FileType::kFifo, 0644), CodeForType(vfs::FileType::kFifo, 0644));
}

TEST_F(ColorTest, AnExtensionEntryColoursARegularFileAndFoldsCase) {
  const Palette palette = Palette::FromLsColors("*.tar=01;31:*.md=32");
  EXPECT_THAT(palette.CodeFor("archive.tar", vfs::FileType::kRegular, 0644), "01;31");
  EXPECT_THAT(palette.CodeFor("ARCHIVE.TAR", vfs::FileType::kRegular, 0644), "01;31");  // themed both ways
  EXPECT_THAT(palette.CodeFor("notes.md", vfs::FileType::kRegular, 0644), "32");
  EXPECT_THAT(palette.CodeFor("notes.rst", vfs::FileType::kRegular, 0644), IsEmpty());
}

TEST_F(ColorTest, TheExecutableBitWinsOverAnExtension) {
  // ls's own order: `ex` is consulted before the extension table, so an executable `*.sh` is
  // coloured as an executable rather than as a script file.
  const Palette palette = Palette::FromLsColors("ex=01;32:*.sh=33");
  EXPECT_THAT(palette.CodeFor("run.sh", vfs::FileType::kRegular, 0755), "01;32");
  EXPECT_THAT(palette.CodeFor("run.sh", vfs::FileType::kRegular, 0644), "33");
}

TEST_F(ColorTest, AnEmptyValueMeansNoColourRatherThanNoOpinion) {
  // `fi=` / `di=` is how a theme says "leave these plain"; it must not fall back to xff's scheme.
  const Palette palette = Palette::FromLsColors("di=:fi=");
  EXPECT_THAT(palette.CodeFor("dir", vfs::FileType::kDirectory, 0755), IsEmpty());
  EXPECT_THAT(palette.CodeFor("a.txt", vfs::FileType::kRegular, 0644), IsEmpty());
}

TEST_F(ColorTest, AMalformedLsColorsIsIgnoredEntryByEntry) {
  // A broken variable is not worth refusing to list files over, and ls ignores such entries too.
  const Palette palette = Palette::FromLsColors("garbage:di=01;35:=nokey:*=nodot:");
  EXPECT_THAT(palette.CodeFor("dir", vfs::FileType::kDirectory, 0755), "01;35");
  EXPECT_THAT(palette.CodeFor("a.txt", vfs::FileType::kRegular, 0644), IsEmpty());
}

}  // namespace
}  // namespace xff::color
