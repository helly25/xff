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

#include "xff/language/language.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::language {
namespace {

using ::testing::Eq;
using ::testing::IsEmpty;

struct LanguageTest : ::testing::Test {};

TEST_F(LanguageTest, ByExtension) {
  EXPECT_THAT(LanguageForName("main.cc"), Eq("C++"));
  EXPECT_THAT(LanguageForName("main.cpp"), Eq("C++"));
  EXPECT_THAT(LanguageForName("util.h"), Eq("C"));  // linguist's default for .h
  EXPECT_THAT(LanguageForName("a.py"), Eq("Python"));
  EXPECT_THAT(LanguageForName("a.rs"), Eq("Rust"));
  EXPECT_THAT(LanguageForName("a.go"), Eq("Go"));
  EXPECT_THAT(LanguageForName("a.ts"), Eq("TypeScript"));
  EXPECT_THAT(LanguageForName("view.m"), Eq("Objective-C"));  // linguist's default for .m
}

TEST_F(LanguageTest, ExtensionIsCaseInsensitive) {
  EXPECT_THAT(LanguageForName("A.PY"), Eq("Python"));
  EXPECT_THAT(LanguageForName("MAIN.CPP"), Eq("C++"));
}

TEST_F(LanguageTest, ByFilename) {
  EXPECT_THAT(LanguageForName("Makefile"), Eq("Makefile"));
  EXPECT_THAT(LanguageForName("GNUmakefile"), Eq("Makefile"));
  EXPECT_THAT(LanguageForName("Dockerfile"), Eq("Dockerfile"));
  EXPECT_THAT(LanguageForName("CMakeLists.txt"), Eq("CMake"));
  EXPECT_THAT(LanguageForName("BUILD.bazel"), Eq("Starlark"));
  EXPECT_THAT(LanguageForName("MODULE.bazel"), Eq("Starlark"));
  EXPECT_THAT(LanguageForName(".bashrc"), Eq("Shell"));  // a dotfile: no extension, matched by name
}

TEST_F(LanguageTest, FilenameWinsOverExtension) {
  // CMakeLists.txt is CMake even though `.txt` is not a mapped extension; the exact filename
  // match is consulted first.
  EXPECT_THAT(LanguageForName("CMakeLists.txt"), Eq("CMake"));
}

TEST_F(LanguageTest, UnknownIsEmpty) {
  EXPECT_THAT(LanguageForName("photo.jpg"), IsEmpty());  // an extension, but not a language
  EXPECT_THAT(LanguageForName("README"), IsEmpty());     // no extension, no filename match
  EXPECT_THAT(LanguageForName("a.unknownext"), IsEmpty());
  EXPECT_THAT(LanguageForName(""), IsEmpty());
}

}  // namespace
}  // namespace xff::language
