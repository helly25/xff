// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/asar/asar_fs.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/digest/digest.h"
#include "mbo/testing/status.h"
#include "nlohmann/json.hpp"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::asar {
namespace {

using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;

using Json = ::nlohmann::ordered_json;
namespace fs = ::std::filesystem;

void AppendU32(std::string& out, std::uint32_t value) {
  out.push_back(static_cast<char>(value & 0xffU));
  out.push_back(static_cast<char>((value >> 8U) & 0xffU));
  out.push_back(static_cast<char>((value >> 16U) & 0xffU));
  out.push_back(static_cast<char>((value >> 24U) & 0xffU));
}

void StoreU32(std::string& out, std::size_t offset, std::uint32_t value) {
  for (std::size_t index = 0; index < 4; ++index) {
    out[offset + index] = static_cast<char>((value >> (index * 8U)) & 0xffU);
  }
}

std::string Sha256(std::string_view content) {
  return mbo::digest::ToHexString(mbo::digest::sha256::Digest(content));
}

Json Integrity(std::string_view content, std::size_t block_size) {
  Json blocks = Json::array();
  for (std::size_t offset = 0; offset < content.size(); offset += block_size) {
    blocks.push_back(Sha256(content.substr(offset, block_size)));
  }
  return Json{
      {"algorithm", "SHA256"},
      {"hash", Sha256(content)},
      {"blockSize", block_size},
      {"blocks", std::move(blocks)},
  };
}

std::string BuildAsar(const Json& header, std::string_view payload = {}) {
  const std::string json = header.dump();
  std::string header_payload;
  AppendU32(header_payload, static_cast<std::uint32_t>(json.size()));
  header_payload.append(json);
  while (header_payload.size() % 4 != 0) {
    header_payload.push_back('\0');
  }
  std::string header_pickle;
  AppendU32(header_pickle, static_cast<std::uint32_t>(header_payload.size()));
  header_pickle.append(header_payload);
  std::string archive;
  AppendU32(archive, 4);
  AppendU32(archive, static_cast<std::uint32_t>(header_pickle.size()));
  archive.append(header_pickle);
  archive.append(payload);
  return archive;
}

struct AsarFileSystemTest : ::testing::Test {
  void SetUp() override {
    root_ = fs::path(::testing::TempDir()) / ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::error_code ignored;
    fs::remove_all(root_, ignored);
    fs::create_directories(root_);
  }

  fs::path root_;
};

TEST_F(AsarFileSystemTest, PackedTreeLinksAndIntegrityAreOneReadOnlyFilesystem) {
  Json header;
  header["files"]["dir"]["files"]["hello.txt"] = {
      {"size", 5},
      {"offset", "0"},
      {"executable", true},
      {"integrity", Integrity("hello", 2)},
  };
  header["files"]["alias"] = {{"link", "dir/hello.txt"}};
  header["integrity"] = {{"incidental", "root metadata is not a file"}};
  MBO_ASSERT_OK_AND_ASSIGN(
      const AsarFileSystem asar, AsarFileSystem::OpenBytes("app.asar", BuildAsar(header, "hello")));

  EXPECT_THAT(
      asar.ReadDir("app.asar"),
      IsOkAndHolds(ElementsAre(
          AllOf(Field("name", &vfs::Entry::name, "alias"), Field("type", &vfs::Entry::type, vfs::FileType::kSymlink)),
          AllOf(
              Field("name", &vfs::Entry::name, "dir"), Field("type", &vfs::Entry::type, vfs::FileType::kDirectory)))));
  EXPECT_THAT(
      asar.ReadDir("app.asar!dir"), IsOkAndHolds(ElementsAre(AllOf(
                                        Field("name", &vfs::Entry::name, "hello.txt"),
                                        Field("source", &vfs::Entry::source, vfs::Source::kArchiveMember),
                                        Field("read_only", &vfs::Entry::read_only, IsTrue())))));
  EXPECT_THAT(
      asar.Stat("app.asar!dir/hello.txt", false),
      IsOkAndHolds(AllOf(
          Field("size", &vfs::Metadata::size, 5), Field("mode", &vfs::Metadata::mode, 0555),
          Field("type", &vfs::Metadata::type, vfs::FileType::kRegular))));
  EXPECT_THAT(asar.ReadContent("app.asar!dir/hello.txt"), IsOkAndHolds("hello"));
  EXPECT_THAT(asar.ReadContent("app.asar!dir/hello.txt"), IsOkAndHolds("hello"));  // cached read
  EXPECT_THAT(asar.ReadLink("app.asar!alias"), IsOkAndHolds("dir/hello.txt"));
  EXPECT_THAT(asar.FsType("app.asar"), IsOkAndHolds("asar"));
  EXPECT_THAT(asar.IsCaseSensitive("app.asar"), IsOkAndHolds(IsTrue()));
  EXPECT_THAT(asar.Access("app.asar!dir/hello.txt", vfs::AccessMode::kRead), IsTrue());
  EXPECT_THAT(asar.Access("app.asar!dir/hello.txt", vfs::AccessMode::kExecute), IsTrue());
  EXPECT_THAT(asar.Access("app.asar!dir/hello.txt", vfs::AccessMode::kWrite), IsFalse());
  EXPECT_THAT(asar.Remove("app.asar!dir/hello.txt"), StatusIs(absl::StatusCode::kPermissionDenied));
}

TEST_F(AsarFileSystemTest, UnpackedMembersReadFromTheValidatedSiblingTree) {
  const std::string content = "outside";
  Json header;
  header["files"]["native"]["unpacked"] = true;
  header["files"]["native"]["files"]["addon.node"] = {
      {"size", content.size()},
      {"unpacked", true},
      {"integrity", Integrity(content, 1'024)},
  };
  const fs::path archive = root_ / "app.asar";
  const fs::path unpacked = root_ / "app.asar.unpacked/native";
  fs::create_directories(unpacked);
  std::ofstream(archive, std::ios::binary) << BuildAsar(header);
  std::ofstream(unpacked / "addon.node", std::ios::binary) << content;

  MBO_ASSERT_OK_AND_ASSIGN(const AsarFileSystem asar, AsarFileSystem::Open(archive.string()));
  EXPECT_THAT(asar.ReadContent(absl::StrCat(archive.string(), "!native/addon.node")), IsOkAndHolds(content));

  std::ofstream(unpacked / "addon.node", std::ios::binary | std::ios::app) << "too much";
  MBO_ASSERT_OK_AND_ASSIGN(const AsarFileSystem changed, AsarFileSystem::Open(archive.string()));
  EXPECT_THAT(
      changed.ReadContent(absl::StrCat(archive.string(), "!native/addon.node")),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("larger than its header size")));
}

TEST_F(AsarFileSystemTest, EmptyFilesHaveNoIntegrityBlocks) {
  Json header;
  header["files"]["empty"] = {{"size", 0}, {"offset", "0"}, {"integrity", Integrity({}, 1'024)}};
  MBO_ASSERT_OK_AND_ASSIGN(const AsarFileSystem asar, AsarFileSystem::OpenBytes("app.asar", BuildAsar(header)));
  EXPECT_THAT(asar.ReadContent("app.asar!empty"), IsOkAndHolds(""));
}

TEST_F(AsarFileSystemTest, CorruptClaimsAreDataLossAndUnclaimedBytesFallThrough) {
  EXPECT_THAT(AsarFileSystem::OpenBytes("notes.txt", "not an archive"), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      AsarFileSystem::OpenBytes("broken.asar", "not an archive"),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("size pickle")));

  std::string oversized_header;
  constexpr std::uint32_t kOversizedHeader = (64U * 1'024U * 1'024U) + 1U;
  AppendU32(oversized_header, 4);
  AppendU32(oversized_header, kOversizedHeader);
  EXPECT_THAT(AsarFileSystem::OpenBytes("notes.txt", oversized_header), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      AsarFileSystem::OpenBytes("broken.asar", std::move(oversized_header)),
      StatusIs(absl::StatusCode::kResourceExhausted, HasSubstr("safety limit")));

  Json outside;
  outside["files"]["bad/name"] = {{"size", 0}, {"offset", "0"}};
  EXPECT_THAT(
      AsarFileSystem::OpenBytes("bad.asar", BuildAsar(outside)),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("invalid entry name")));

  Json beyond;
  beyond["files"]["huge"] = {{"size", 9}, {"offset", "0"}};
  EXPECT_THAT(
      AsarFileSystem::OpenBytes("bad.asar", BuildAsar(beyond)),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("beyond the archive")));
}

TEST_F(AsarFileSystemTest, RejectsMalformedPicklesAndHeaders) {
  Json valid_header;
  valid_header["files"] = Json::object();
  const std::string valid = BuildAsar(valid_header);

  std::string wrong_size_pickle = valid;
  StoreU32(wrong_size_pickle, 0, 8);
  EXPECT_THAT(
      AsarFileSystem::OpenBytes("bad.asar", std::move(wrong_size_pickle)),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("one UInt32")));

  std::string short_header_pickle = valid;
  StoreU32(short_header_pickle, 4, 4);
  EXPECT_THAT(
      AsarFileSystem::OpenBytes("bad.asar", std::move(short_header_pickle)),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("too short")));

  std::string inconsistent_payload = valid;
  StoreU32(inconsistent_payload, 8, 0);
  EXPECT_THAT(
      AsarFileSystem::OpenBytes("bad.asar", std::move(inconsistent_payload)),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("inconsistent payload")));

  std::string impossible_json_size = valid;
  StoreU32(impossible_json_size, 12, 1);
  EXPECT_THAT(
      AsarFileSystem::OpenBytes("bad.asar", std::move(impossible_json_size)),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("does not fit")));

  std::string invalid_json = valid;
  invalid_json[16] = '!';
  EXPECT_THAT(
      AsarFileSystem::OpenBytes("bad.asar", std::move(invalid_json)),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("not a JSON object")));

  EXPECT_THAT(
      AsarFileSystem::OpenBytes("bad.asar", BuildAsar(Json::array())),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("not a JSON object")));
}

TEST_F(AsarFileSystemTest, RejectsMalformedDirectoryAndFileRecords) {
  const auto expect_bad = [](std::string_view name, Json entry, std::string_view message,
                             std::string_view payload = {}) {
    SCOPED_TRACE(name);
    Json header;
    header["files"][name] = std::move(entry);
    EXPECT_THAT(
        AsarFileSystem::OpenBytes("bad.asar", BuildAsar(header, payload)),
        StatusIs(absl::StatusCode::kDataLoss, HasSubstr(message)));
  };

  EXPECT_THAT(
      AsarFileSystem::OpenBytes("bad.asar", BuildAsar(Json::object())),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("root must contain")));
  expect_bad("plain", 3, "not an object");
  expect_bad(".", Json::object(), "invalid entry name");
  expect_bad("bad/name", Json::object(), "invalid entry name");
  expect_bad("dir", {{"files", 3}}, "non-object `files`");
  expect_bad("file", Json::object(), "has no `size`");
  expect_bad("file", {{"size", -1}, {"offset", "0"}}, "negative `size`");
  expect_bad("file", {{"size", 1.5}, {"offset", "0"}}, "non-integer", "xx");
  expect_bad("file", {{"size", 1}, {"offset", 0}}, "no string `offset`", "x");
  expect_bad("file", {{"size", 1}, {"offset", "no"}}, "non-numeric `offset`", "x");
  expect_bad("file", {{"size", 0}, {"offset", "0"}, {"executable", "yes"}}, "non-boolean `executable`");
  expect_bad("file", {{"size", 0}, {"offset", "0"}, {"unpacked", 1}}, "non-boolean `unpacked`");
  expect_bad("link", {{"link", ""}}, "invalid `link`");
  expect_bad("link", {{"link", 7}}, "invalid `link`");
}

TEST_F(AsarFileSystemTest, IntegrityMetadataIsValidatedAndNeverBecomesAnEntry) {
  Json bad_record;
  bad_record["files"]["x"] = {
      {"size", 1},
      {"offset", "0"},
      {"integrity", {{"algorithm", "MD5"}}},
  };
  EXPECT_THAT(
      AsarFileSystem::OpenBytes("bad.asar", BuildAsar(bad_record, "x")),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("algorithm")));

  Json wrong_hash;
  wrong_hash["files"]["x"] = {
      {"size", 1},
      {"offset", "0"},
      {"integrity", Integrity("y", 1)},
  };
  MBO_ASSERT_OK_AND_ASSIGN(
      const AsarFileSystem asar, AsarFileSystem::OpenBytes("app.asar", BuildAsar(wrong_hash, "x")));
  EXPECT_THAT(
      asar.ReadContent("app.asar!x"), StatusIs(absl::StatusCode::kDataLoss, HasSubstr("integrity check failed")));
  EXPECT_THAT(asar.ReadDir("app.asar"), IsOkAndHolds(ElementsAre(Field("name", &vfs::Entry::name, "x"))));

  Json wrong_block;
  wrong_block["files"]["x"] = {
      {"size", 1},
      {"offset", "0"},
      {"integrity", Integrity("x", 1)},
  };
  wrong_block["files"]["x"]["integrity"]["blocks"][0] = Sha256("y");
  MBO_ASSERT_OK_AND_ASSIGN(
      const AsarFileSystem block_asar, AsarFileSystem::OpenBytes("app.asar", BuildAsar(wrong_block, "x")));
  EXPECT_THAT(
      block_asar.ReadContent("app.asar!x"),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("block integrity check failed")));
}

TEST_F(AsarFileSystemTest, EveryIntegrityFieldIsValidated) {
  const auto expect_bad = [](Json integrity, std::string_view message) {
    Json header;
    header["files"]["x"] = {{"size", 1}, {"offset", "0"}, {"integrity", std::move(integrity)}};
    EXPECT_THAT(
        AsarFileSystem::OpenBytes("bad.asar", BuildAsar(header, "x")),
        StatusIs(absl::StatusCode::kDataLoss, HasSubstr(message)));
  };
  expect_bad("not an object", "non-object");
  expect_bad({{"algorithm", "SHA1"}}, "algorithm");
  expect_bad({{"algorithm", "SHA256"}, {"hash", "short"}}, "integrity hash");
  expect_bad({{"algorithm", "SHA256"}, {"hash", Sha256("x")}}, "no integrity block size");
  expect_bad(
      {{"algorithm", "SHA256"}, {"hash", Sha256("x")}, {"blockSize", 0}, {"blocks", Json::array()}},
      "zero integrity block size");
  expect_bad({{"algorithm", "SHA256"}, {"hash", Sha256("x")}, {"blockSize", 1}, {"blocks", "none"}}, "block array");
  expect_bad(
      {{"algorithm", "SHA256"}, {"hash", Sha256("x")}, {"blockSize", 1}, {"blocks", Json::array()}}, "wrong number");
  expect_bad(
      {{"algorithm", "SHA256"}, {"hash", Sha256("x")}, {"blockSize", 1}, {"blocks", {"short"}}},
      "invalid integrity block hash");
}

TEST_F(AsarFileSystemTest, VirtualFilesystemOperationsReportPreciseFailures) {
  Json header;
  header["files"]["dir"]["files"]["x"] = {{"size", 1}, {"offset", "0"}};
  header["files"]["link"] = {{"link", "dir/x"}};
  MBO_ASSERT_OK_AND_ASSIGN(const AsarFileSystem asar, AsarFileSystem::OpenBytes("app.asar", BuildAsar(header, "x")));

  EXPECT_THAT(asar.Stat("elsewhere", false), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(asar.Stat("app.asar!missing", false), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(asar.ReadDir("app.asar!missing"), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(asar.ReadDir("app.asar!dir/x"), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(asar.ReadLink("app.asar"), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(asar.ReadLink("app.asar!missing"), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(asar.ReadLink("app.asar!dir/x"), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(asar.ReadContent("app.asar"), StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(asar.ReadContent("app.asar!missing"), StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(asar.ReadContent("app.asar!dir"), StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(asar.Access("app.asar!missing", vfs::AccessMode::kRead), IsFalse());
  EXPECT_THAT(asar.Access("app.asar!dir/x", vfs::AccessMode::kExecute), IsFalse());
  EXPECT_THAT(
      asar.Stat("app.asar", false), IsOkAndHolds(Field("type", &vfs::Metadata::type, vfs::FileType::kDirectory)));
  EXPECT_THAT(asar.ReadDir("app.asar"), IsOkAndHolds(Not(ElementsAre())));

  MBO_ASSERT_OK_AND_ASSIGN(
      const AsarFileSystem custom,
      AsarFileSystem::OpenBytes("app.asar", BuildAsar(header, "x"), archive::MemberPathOptions{.separator = "#"}));
  EXPECT_THAT(custom.ReadContent("app.asar#dir/x"), IsOkAndHolds("x"));
}

}  // namespace
}  // namespace xff::asar
