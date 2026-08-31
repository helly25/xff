// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/archive/archive_backend.h"

namespace xff::brotli {
namespace {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Field;
using ::testing::IsTrue;

struct BrotliRegisterTest : ::testing::Test {};

TEST_F(BrotliRegisterTest, LinkingTheExtraExtendsTheArchiveReaderAndWriterVocabulary) {
  EXPECT_THAT(archive::ContainerSupportAvailable(), IsTrue());
  EXPECT_THAT(archive::LooksLikeContainerName("bundle.tar.br"), IsTrue());
  EXPECT_THAT(archive::LooksLikeContainerName("bundle.tbr"), IsTrue());
  EXPECT_THAT(
      archive::ContainerReadFormats(), Contains(AllOf(
                                           Field("name", &archive::ReadFormatInfo::name, "tar"),
                                           Field("suffixes", &archive::ReadFormatInfo::suffixes, Contains(".tbr")))));
  EXPECT_THAT(archive::ContainerPackFormats(), Contains("tar.br"));
  EXPECT_THAT(archive::ContainerPackFormats(), Contains("tbr"));
  EXPECT_THAT(
      archive::ContainerPackVocabulary(),
      Contains(AllOf(
          Field("name", &archive::PackOptionInfo::name, "framing"),
          Field("formats", &archive::PackOptionInfo::formats, Contains("tar.br")))));
}

}  // namespace
}  // namespace xff::brotli
