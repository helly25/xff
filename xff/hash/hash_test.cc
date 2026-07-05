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

#include "xff/hash/hash.h"

#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::hash {
namespace {

using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsSupersetOf;
using ::testing::IsTrue;
using ::testing::Optional;
using ::testing::SizeIs;

struct HashTest : ::testing::Test {};

// Known NIST / RFC vectors. mbo::digest owns the spec conformance (vector-pinned in its own
// tests); these anchor xff's name dispatch and encoding wiring per algorithm family.
TEST_F(HashTest, HashDataHexKnownVectors) {
  EXPECT_THAT(HashData("md5", "abc"), Optional(Eq("900150983cd24fb0d6963f7d28e17f72")));
  EXPECT_THAT(HashData("sha1", "abc"), Optional(Eq("a9993e364706816aba3e25717850c26c9cd0d89d")));
  EXPECT_THAT(
      HashData("sha256", "abc"), Optional(Eq("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")));
  EXPECT_THAT(HashData("sha256", ""), Optional(Eq("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")));
  EXPECT_THAT(
      HashData("sha512", "abc"), Optional(
                                     Eq("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                                        "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f")));
  EXPECT_THAT(
      HashData("sha3_256", "abc"), Optional(Eq("3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532")));
}

// base64 of the raw sha256("abc") bytes is the well-known Subresource-Integrity body.
TEST_F(HashTest, HashDataBase64) {
  EXPECT_THAT(
      HashData("sha256", "abc", Encoding::kBase64), Optional(Eq("ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=")));
}

TEST_F(HashTest, HashDataUnknownAlgorithmIsNullopt) {
  EXPECT_THAT(HashData("crc32", "abc"), Eq(std::nullopt));
}

TEST_F(HashTest, ParseEncoding) {
  EXPECT_THAT(ParseEncoding("hex"), Optional(Eq(Encoding::kHex)));
  EXPECT_THAT(ParseEncoding("base64"), Optional(Eq(Encoding::kBase64)));
  EXPECT_THAT(ParseEncoding("b64"), Eq(std::nullopt));
  EXPECT_THAT(ParseEncoding(""), Eq(std::nullopt));
}

TEST_F(HashTest, IsAlgorithmAndNames) {
  EXPECT_THAT(IsAlgorithm("sha256"), ::testing::IsTrue());
  EXPECT_THAT(IsAlgorithm("blake3"), ::testing::IsTrue());
  EXPECT_THAT(IsAlgorithm("crc32"), ::testing::IsFalse());
  // The full fixed-size set is exposed, sorted, and every name round-trips through IsAlgorithm.
  EXPECT_THAT(
      AlgorithmNames(), IsSupersetOf(
                            {"blake2b", "blake2b_256", "blake3", "md5", "sha1", "sha224", "sha256", "sha384",
                             "sha3_224", "sha3_256", "sha3_384", "sha3_512", "sha512", "sha512_224", "sha512_256"}));
  EXPECT_THAT(AlgorithmNames(), SizeIs(15));
}

TEST_F(HashTest, HashFileReadsAndHashesContent) {
  const std::string path = std::string(::testing::TempDir()) + "/xff_hash_test_abc";
  { std::ofstream(path) << "abc"; }
  EXPECT_THAT(
      HashFile("sha256", path), Optional(Eq("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")));
  EXPECT_THAT(
      HashFile("sha256", path, Encoding::kBase64), Optional(Eq("ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=")));
  std::remove(path.c_str());
}

TEST_F(HashTest, HashFileMissingOrBadAlgorithmIsNullopt) {
  EXPECT_THAT(HashFile("sha256", std::string(::testing::TempDir()) + "/xff_hash_absent"), Eq(std::nullopt));
  const std::string path = std::string(::testing::TempDir()) + "/xff_hash_test_bad";
  { std::ofstream(path) << "abc"; }
  EXPECT_THAT(HashFile("crc32", path), Eq(std::nullopt));  // unknown algo -> nullopt even for a readable file
  std::remove(path.c_str());
}

}  // namespace
}  // namespace xff::hash
