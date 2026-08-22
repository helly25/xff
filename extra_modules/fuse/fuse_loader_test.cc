// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
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
//
// These tests are environment-AGNOSTIC on purpose: whether a machine has libfuse3 is exactly what
// the loader reports, so a test may not assume either answer (Linux CI images tend to have it,
// macOS CI does not, developer machines vary). What is pinned instead are the loader's invariants
// in BOTH states.

#include "xff/fuse/fuse_loader.h"

#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::fuse {
namespace {

using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::SizeIs;

struct FuseLoaderTest : ::testing::Test {};

TEST_F(FuseLoaderTest, TheProbeIsStableAcrossCalls) {
  const FuseLoader& first = FuseLoader::Instance();
  const FuseLoader& second = FuseLoader::Instance();
  EXPECT_THAT(&second, Eq(&first));
  EXPECT_THAT(FuseAvailable(), Eq(first.available()));
}

TEST_F(FuseLoaderTest, AvailableMeansEveryRequiredSymbolResolves) {
  const FuseLoader& loader = FuseLoader::Instance();
  if (!loader.available()) {
    GTEST_SKIP() << "no FUSE3 on this machine: " << loader.error();
  }
  EXPECT_THAT(loader.library(), Not(IsEmpty()));
  EXPECT_THAT(loader.error(), IsEmpty());
  for (const std::string_view symbol : RequiredSymbols()) {
    EXPECT_THAT(loader.Symbol(symbol), NotNull()) << symbol;
  }
}

TEST_F(FuseLoaderTest, UnavailableCarriesTheReasonAndResolvesNothing) {
  const FuseLoader& loader = FuseLoader::Instance();
  if (loader.available()) {
    GTEST_SKIP() << "FUSE3 present (" << loader.library() << "); the unavailable path is not reachable here";
  }
  EXPECT_THAT(loader.library(), IsEmpty());
  EXPECT_THAT(loader.error(), Not(IsEmpty()));
  EXPECT_THAT(loader.Symbol("fuse_session_new"), IsNull());
}

TEST_F(FuseLoaderTest, AnUnknownSymbolIsNullEvenWhenAvailable) {
  EXPECT_THAT(FuseLoader::Instance().Symbol("xff_definitely_not_a_fuse_symbol"), IsNull());
}

TEST_F(FuseLoaderTest, TheRequiredSetCoversTheServersCallSurface) {
  // The server needs the session lifecycle, the mount pair, the reply calls, the direntry builder,
  // readlink and the argv deallocator; a shrink of this list would let "available" lie. SizeIs guards accidental
  // deletion; the names themselves are the SOT in fuse_loader.cc.
  EXPECT_THAT(RequiredSymbols(), SizeIs(15));
}

}  // namespace
}  // namespace xff::fuse
