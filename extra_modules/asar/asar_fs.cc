// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/asar/asar_fs.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "mbo/digest/digest.h"
#include "mbo/status/status_macros.h"
#include "nlohmann/json.hpp"
#include "xff/archive/member_path.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::asar {
namespace {

using Json = ::nlohmann::ordered_json;

constexpr std::uint64_t kMaximumHeaderSize = 64ULL * 1'024 * 1'024;
constexpr std::size_t kMaximumTreeDepth = 256;
constexpr std::uint64_t kVirtualDeviceBit = std::uint64_t{1} << 63U;

std::uint32_t ReadU32(std::string_view data, std::size_t offset) {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset]))
         | (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 1])) << 8U)
         | (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 2])) << 16U)
         | (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 3])) << 24U);
}

bool ClaimsAsar(std::string_view container) {
  return absl::EndsWithIgnoreCase(container, ".asar");
}

absl::Status HeaderError(std::string_view container, std::string_view message, bool claimed) {
  const std::string text = absl::StrCat("invalid ASAR '", container, "': ", message);
  return claimed ? absl::DataLossError(text) : absl::InvalidArgumentError(text);
}

absl::Status DataError(std::string_view container, std::string_view message) {
  return absl::DataLossError(absl::StrCat("invalid ASAR '", container, "': ", message));
}

absl::StatusOr<std::uint64_t> HeaderSize(
    std::string_view first_eight,
    std::uint64_t archive_size,
    std::string_view container) {
  const bool claimed = ClaimsAsar(container);
  if (first_eight.size() < 8) {
    return HeaderError(container, "the size pickle is truncated", claimed);
  }
  if (ReadU32(first_eight, 0) != 4) {
    return HeaderError(container, "the size pickle does not contain one UInt32", claimed);
  }
  const std::uint64_t size = ReadU32(first_eight, 4);
  if (size < 8) {
    return HeaderError(container, "the header pickle is too short", claimed);
  }
  if (size > kMaximumHeaderSize) {
    const std::string message =
        absl::StrCat("ASAR header is ", size, " bytes, over the ", kMaximumHeaderSize, " byte safety limit");
    return claimed ? absl::ResourceExhaustedError(message) : absl::InvalidArgumentError(message);
  }
  if (size > archive_size - 8) {
    return HeaderError(container, "the header pickle extends beyond the archive", claimed);
  }
  return size;
}

absl::StatusOr<Json> DecodeHeader(std::string_view prefix, std::uint64_t archive_size, std::string_view container) {
  MBO_ASSIGN_OR_RETURN(const std::uint64_t header_size, HeaderSize(prefix.substr(0, 8), archive_size, container));
  if (prefix.size() < 8 + header_size) {
    return HeaderError(container, "the header pickle is truncated", ClaimsAsar(container));
  }
  const std::string_view pickle = prefix.substr(8, header_size);
  const std::uint64_t payload_size = ReadU32(pickle, 0);
  if (payload_size + 4 != header_size || payload_size < 4) {
    return HeaderError(container, "the header pickle has an inconsistent payload size", ClaimsAsar(container));
  }
  const std::uint32_t json_size = ReadU32(pickle, 4);
  const std::uint64_t padded_json_size = (static_cast<std::uint64_t>(json_size) + 3U) & ~std::uint64_t{3};
  if (4 + padded_json_size != payload_size) {
    return HeaderError(container, "the JSON string does not fit its pickle", ClaimsAsar(container));
  }
  Json root = Json::parse(pickle.substr(8, json_size), nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded() || !root.is_object()) {
    return HeaderError(container, "the header is not a JSON object", ClaimsAsar(container));
  }
  return root;
}

std::string ParentOf(std::string_view key) {
  const std::string_view::size_type slash = key.rfind('/');
  return slash == std::string_view::npos ? std::string() : std::string(key.substr(0, slash));
}

std::string_view NameOf(std::string_view key) {
  const std::string_view::size_type slash = key.rfind('/');
  return slash == std::string_view::npos ? key : key.substr(slash + 1);
}

vfs::Metadata DirectoryMetadata(std::uint64_t device = 0, std::uint64_t inode = 0) {
  return vfs::Metadata{
      .type = vfs::FileType::kDirectory,
      .source = vfs::Source::kArchiveMember,
      .mode = 0555,
      .nlink = 1,
      .ino = inode,
      .dev = device,
      .atime = absl::UnixEpoch(),
      .mtime = absl::UnixEpoch(),
      .ctime = absl::UnixEpoch(),
  };
}

bool IsHexDigest(std::string_view value) {
  return value.size() == 64 && std::ranges::all_of(value, [](unsigned char ch) { return std::isxdigit(ch) != 0; });
}

std::string Sha256(std::string_view content) {
  return mbo::digest::ToHexString(mbo::digest::sha256::Digest(content));
}

absl::StatusOr<std::string> ReadRange(
    std::string_view path,
    std::uint64_t offset,
    std::uint64_t size,
    bool require_end = false) {
  if (!std::in_range<std::size_t>(size) || !std::in_range<std::streamsize>(size)
      || !std::in_range<std::streamoff>(offset)) {
    return absl::ResourceExhaustedError(absl::StrCat("ASAR member is too large to read: ", path));
  }
  // XFF_HOST_IO: ASAR adapter reads its explicitly selected container file.
  std::ifstream input(std::string(path), std::ios::binary);
  if (!input) {
    return absl::NotFoundError(absl::StrCat("cannot read '", path, "'"));
  }
  input.seekg(static_cast<std::streamoff>(offset));
  if (!input) {
    return absl::DataLossError(absl::StrCat("cannot seek to ASAR content in '", path, "'"));
  }
  std::string content(static_cast<std::size_t>(size), '\0');
  input.read(content.data(), static_cast<std::streamsize>(content.size()));
  if (input.gcount() != static_cast<std::streamsize>(content.size())) {
    return absl::DataLossError(absl::StrCat("ASAR member content is truncated in '", path, "'"));
  }
  // XFF_HOST_IO: inspect the adapter's host container stream for trailing bytes.
  if (require_end && input.peek() != std::ifstream::traits_type::eof()) {
    return absl::DataLossError(absl::StrCat("ASAR unpacked member is larger than its header size in '", path, "'"));
  }
  return content;
}

}  // namespace

class AsarParser {
 public:
  AsarParser(AsarFileSystem& fs, const Json& root, std::uint64_t archive_size)
      : fs_(fs), root_(root), archive_size_(archive_size), device_(kVirtualDeviceBit | absl::HashOf(fs.container_)) {}

  absl::Status Parse() {
    const auto files = root_.find("files");
    if (files == root_.end() || !files->is_object()) {
      return DataError(fs_.container_, "the root must contain a `files` object");
    }
    return ParseDirectory(*files, std::string(), /*inherited_unpacked=*/false, /*depth=*/0);
  }

 private:
  absl::StatusOr<std::uint64_t> NonNegativeInteger(const Json& value, std::string_view path, std::string_view field)
      const {
    if (value.is_number_unsigned()) {
      return value.get<std::uint64_t>();
    }
    if (value.is_number_integer()) {
      const std::int64_t signed_value = value.get<std::int64_t>();
      if (signed_value >= 0) {
        return static_cast<std::uint64_t>(signed_value);
      }
    }
    return DataError(fs_.container_, absl::StrCat(path, " has a non-integer or negative `", field, "`"));
  }

  absl::StatusOr<bool> Boolean(const Json& node, std::string_view field, bool fallback, std::string_view path) const {
    const auto value = node.find(field);
    if (value == node.end()) {
      return fallback;
    }
    if (!value->is_boolean()) {
      return DataError(fs_.container_, absl::StrCat(path, " has a non-boolean `", field, "`"));
    }
    return value->get<bool>();
  }

  absl::StatusOr<AsarFileSystem::Integrity> ParseIntegrity(const Json& value, std::string_view path, std::uint64_t size)
      const {
    if (!value.is_object()) {
      return DataError(fs_.container_, absl::StrCat(path, " has a non-object `integrity` record"));
    }
    const auto algorithm = value.find("algorithm");
    const auto hash = value.find("hash");
    const auto block_size = value.find("blockSize");
    const auto blocks = value.find("blocks");
    if (algorithm == value.end() || !algorithm->is_string() || algorithm->get<std::string_view>() != "SHA256") {
      return DataError(fs_.container_, absl::StrCat(path, " has an unsupported integrity algorithm"));
    }
    if (hash == value.end() || !hash->is_string() || !IsHexDigest(hash->get<std::string_view>())) {
      return DataError(fs_.container_, absl::StrCat(path, " has an invalid integrity hash"));
    }
    if (block_size == value.end()) {
      return DataError(fs_.container_, absl::StrCat(path, " has no integrity block size"));
    }
    MBO_ASSIGN_OR_RETURN(const std::uint64_t size_per_block, NonNegativeInteger(*block_size, path, "blockSize"));
    if (size_per_block == 0) {
      return DataError(fs_.container_, absl::StrCat(path, " has a zero integrity block size"));
    }
    if (blocks == value.end() || !blocks->is_array()) {
      return DataError(fs_.container_, absl::StrCat(path, " has no integrity block array"));
    }
    const std::uint64_t expected_blocks =
        (size / size_per_block) + static_cast<std::uint64_t>(size % size_per_block != 0);
    if (blocks->size() != expected_blocks) {
      return DataError(fs_.container_, absl::StrCat(path, " has the wrong number of integrity block hashes"));
    }
    AsarFileSystem::Integrity integrity{
        .hash = absl::AsciiStrToLower(hash->get<std::string>()),
        .block_size = size_per_block,
    };
    integrity.blocks.reserve(blocks->size());
    for (const Json& block : *blocks) {
      if (!block.is_string() || !IsHexDigest(block.get<std::string_view>())) {
        return DataError(fs_.container_, absl::StrCat(path, " has an invalid integrity block hash"));
      }
      integrity.blocks.push_back(absl::AsciiStrToLower(block.get<std::string>()));
    }
    return integrity;
  }

  absl::Status ParseDirectory(
      const Json& files,
      const std::string& parent,
      bool inherited_unpacked,
      std::size_t depth) {
    if (depth > kMaximumTreeDepth) {
      return absl::ResourceExhaustedError(absl::StrCat("ASAR directory tree exceeds ", kMaximumTreeDepth, " levels"));
    }
    for (const auto& [name, child] : files.items()) {
      if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string::npos) {
        return DataError(fs_.container_, absl::StrCat("invalid entry name `", name, "`"));
      }
      if (!child.is_object()) {
        return DataError(fs_.container_, absl::StrCat("entry `", name, "` is not an object"));
      }
      const std::string path = parent.empty() ? name : absl::StrCat(parent, "/", name);
      MBO_RETURN_IF_ERROR(ParseEntry(child, path, inherited_unpacked, depth));
    }
    return absl::OkStatus();
  }

  absl::Status ParseEntry(const Json& child, const std::string& path, bool inherited_unpacked, std::size_t depth) {
    MBO_ASSIGN_OR_RETURN(const bool unpacked, Boolean(child, "unpacked", inherited_unpacked, path));
    if (const auto link = child.find("link"); link != child.end()) {
      return ParseLink(*link, path, unpacked);
    }
    if (const auto children = child.find("files"); children != child.end()) {
      return ParseSubdirectory(*children, path, unpacked, depth);
    }
    return ParseFile(child, path, unpacked);
  }

  absl::Status ParseLink(const Json& link, const std::string& path, bool unpacked) {
    if (!link.is_string() || link.get<std::string_view>().empty()) {
      return DataError(fs_.container_, absl::StrCat(path, " has an invalid `link`"));
    }
    AsarFileSystem::Node node;
    node.metadata = Metadata(vfs::FileType::kSymlink, /*executable=*/false);
    node.link_target = link.get<std::string>();
    node.unpacked = unpacked;
    fs_.nodes_.emplace(path, std::move(node));
    return absl::OkStatus();
  }

  absl::Status ParseSubdirectory(const Json& children, const std::string& path, bool unpacked, std::size_t depth) {
    if (!children.is_object()) {
      return DataError(fs_.container_, absl::StrCat(path, " has a non-object `files` field"));
    }
    AsarFileSystem::Node node;
    node.metadata = DirectoryMetadata(device_, next_inode_++);
    node.unpacked = unpacked;
    fs_.nodes_.emplace(path, std::move(node));
    return ParseDirectory(children, path, unpacked, depth + 1);
  }

  absl::Status ParseFile(const Json& child, const std::string& path, bool unpacked) {
    const auto size_value = child.find("size");
    if (size_value == child.end()) {
      return DataError(fs_.container_, absl::StrCat(path, " has no `size`"));
    }
    MBO_ASSIGN_OR_RETURN(const std::uint64_t size, NonNegativeInteger(*size_value, path, "size"));
    MBO_ASSIGN_OR_RETURN(const bool executable, Boolean(child, "executable", /*fallback=*/false, path));
    AsarFileSystem::Node node;
    node.metadata = Metadata(vfs::FileType::kRegular, executable);
    node.metadata.size = size;
    node.unpacked = unpacked;
    if (!unpacked) {
      MBO_RETURN_IF_ERROR(ParseOffset(child, path, size, node));
    }
    if (const auto integrity = child.find("integrity"); integrity != child.end()) {
      MBO_ASSIGN_OR_RETURN(node.integrity, ParseIntegrity(*integrity, path, size));
    }
    fs_.nodes_.emplace(path, std::move(node));
    return absl::OkStatus();
  }

  absl::Status ParseOffset(const Json& child, const std::string& path, std::uint64_t size, AsarFileSystem::Node& node)
      const {
    const auto offset = child.find("offset");
    if (offset == child.end() || !offset->is_string()) {
      return DataError(fs_.container_, absl::StrCat(path, " has no string `offset`"));
    }
    if (!absl::SimpleAtoi(offset->get<std::string_view>(), &node.offset)) {
      return DataError(fs_.container_, absl::StrCat(path, " has a non-numeric `offset`"));
    }
    if (node.offset > archive_size_ - fs_.data_offset_ || size > archive_size_ - fs_.data_offset_ - node.offset) {
      return DataError(fs_.container_, absl::StrCat(path, " extends beyond the archive"));
    }
    return absl::OkStatus();
  }

  vfs::Metadata Metadata(vfs::FileType type, bool executable) {
    return vfs::Metadata{
        .type = type,
        .source = vfs::Source::kArchiveMember,
        .mode = executable ? 0555U : 0444U,
        .nlink = 1,
        .ino = next_inode_++,
        .dev = device_,
        .atime = absl::UnixEpoch(),
        .mtime = absl::UnixEpoch(),
        .ctime = absl::UnixEpoch(),
    };
  }

  AsarFileSystem& fs_;
  const Json& root_;
  std::uint64_t archive_size_;
  std::uint64_t device_;
  std::uint64_t next_inode_ = 1;
};

AsarFileSystem::AsarFileSystem(AsarFileSystem&& other) noexcept
    : container_(std::move(other.container_)),
      bytes_(std::move(other.bytes_)),
      data_offset_(other.data_offset_),
      options_(other.options_),
      nodes_(std::move(other.nodes_)) {
  // The cache cannot be moved in the initializer list: its source must be locked first.
  const absl::MutexLock lock(other.cache_mutex_);
  // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer)
  content_cache_ = std::move(other.content_cache_);
}

absl::StatusOr<AsarFileSystem> AsarFileSystem::Open(std::string_view container, archive::MemberPathOptions options) {
  // XFF_HOST_IO: ASAR adapter reads its explicitly selected container file.
  std::ifstream input(std::string(container), std::ios::binary);
  if (!input) {
    return absl::NotFoundError(absl::StrCat("cannot read ASAR '", container, "'"));
  }
  input.seekg(0, std::ios::end);
  const std::streamoff end = input.tellg();
  if (end < 0) {
    return absl::DataLossError(absl::StrCat("cannot determine ASAR size: ", container));
  }
  const auto archive_size = static_cast<std::uint64_t>(end);
  input.seekg(0);
  std::string first_eight(8, '\0');
  input.read(first_eight.data(), static_cast<std::streamsize>(first_eight.size()));
  if (input.gcount() != static_cast<std::streamsize>(first_eight.size())) {
    return HeaderError(container, "the size pickle is truncated", ClaimsAsar(container));
  }
  MBO_ASSIGN_OR_RETURN(const std::uint64_t header_size, HeaderSize(first_eight, archive_size, container));
  std::string prefix = std::move(first_eight);
  prefix.resize(static_cast<std::size_t>(8 + header_size));
  input.read(std::span(prefix).subspan(8).data(), static_cast<std::streamsize>(header_size));
  if (input.gcount() != static_cast<std::streamsize>(header_size)) {
    return HeaderError(container, "the header pickle is truncated", ClaimsAsar(container));
  }
  return Parse(std::string(container), std::string(), prefix, archive_size, options);
}

absl::StatusOr<AsarFileSystem> AsarFileSystem::OpenBytes(
    std::string_view container,
    std::string bytes,
    archive::MemberPathOptions options) {
  const std::uint64_t archive_size = bytes.size();
  return Parse(std::string(container), std::move(bytes), std::string(), archive_size, options);
}

absl::StatusOr<AsarFileSystem> AsarFileSystem::Parse(
    std::string container,
    std::string bytes,
    const std::string& header_bytes,
    std::uint64_t archive_size,
    archive::MemberPathOptions options) {
  const std::string_view source = header_bytes.empty() ? std::string_view(bytes) : std::string_view(header_bytes);
  MBO_ASSIGN_OR_RETURN(const Json root, DecodeHeader(source, archive_size, container));
  MBO_ASSIGN_OR_RETURN(const std::uint64_t header_size, HeaderSize(source.substr(0, 8), archive_size, container));
  AsarFileSystem fs(std::move(container), options);
  fs.bytes_ = std::move(bytes);
  fs.data_offset_ = 8 + header_size;
  MBO_RETURN_IF_ERROR(AsarParser(fs, root, archive_size).Parse());
  return fs;
}

std::optional<std::string> AsarFileSystem::MemberKeyOf(std::string_view path) const {
  if (path == container_) {
    return std::string();
  }
  const std::optional<archive::MemberPathParts> parts =
      archive::SplitMemberPath(path, options_, [this](std::string_view candidate) { return candidate == container_; });
  return parts.has_value() ? std::optional<std::string>(parts->member) : std::nullopt;
}

absl::StatusOr<std::vector<vfs::Entry>> AsarFileSystem::ReadDir(std::string_view dir) const {
  const std::optional<std::string> key = MemberKeyOf(dir);
  if (!key.has_value()) {
    return absl::InvalidArgumentError(absl::StrCat("not a path in ", container_, ": ", dir));
  }
  if (!key->empty()) {
    const auto node = nodes_.find(*key);
    if (node == nodes_.end()) {
      return absl::NotFoundError(absl::StrCat("no such ASAR member: ", *key));
    }
    if (node->second.metadata.type != vfs::FileType::kDirectory) {
      return absl::InvalidArgumentError(absl::StrCat("not a directory: ", *key));
    }
  }
  std::vector<vfs::Entry> entries;
  for (const auto& [member, node] : nodes_) {
    if (ParentOf(member) == *key) {
      entries.push_back({
          .path = archive::JoinMemberPath(container_, member, options_),
          .name = std::string(NameOf(member)),
          .type = node.metadata.type,
          .source = vfs::Source::kArchiveMember,
          .read_only = true,
      });
    }
  }
  return entries;
}

absl::StatusOr<vfs::Metadata> AsarFileSystem::Stat(std::string_view path, bool /*follow_symlinks*/) const {
  const std::optional<std::string> key = MemberKeyOf(path);
  if (!key.has_value()) {
    return absl::InvalidArgumentError(absl::StrCat("not a path in ", container_, ": ", path));
  }
  if (key->empty()) {
    return DirectoryMetadata();
  }
  const auto node = nodes_.find(*key);
  return node == nodes_.end()
             ? absl::StatusOr<vfs::Metadata>(absl::NotFoundError(absl::StrCat("no such ASAR member: ", *key)))
             : absl::StatusOr<vfs::Metadata>(node->second.metadata);
}

absl::Status AsarFileSystem::Remove(std::string_view path) const {
  return absl::PermissionDeniedError(absl::StrCat("ASAR members are read-only, cannot remove: ", path));
}

bool AsarFileSystem::Access(std::string_view path, vfs::AccessMode mode) const {
  if (mode == vfs::AccessMode::kWrite) {
    return false;
  }
  const absl::StatusOr<vfs::Metadata> metadata = Stat(path, /*follow_symlinks=*/false);
  if (!metadata.ok()) {
    return false;
  }
  return mode == vfs::AccessMode::kRead ? (metadata->mode & 0444U) != 0 : (metadata->mode & 0111U) != 0;
}

absl::StatusOr<std::string> AsarFileSystem::ReadLink(std::string_view path) const {
  const std::optional<std::string> key = MemberKeyOf(path);
  if (!key.has_value() || key->empty()) {
    return absl::InvalidArgumentError(absl::StrCat("not an ASAR member path: ", path));
  }
  const auto node = nodes_.find(*key);
  if (node == nodes_.end()) {
    return absl::NotFoundError(absl::StrCat("no such ASAR member: ", *key));
  }
  if (node->second.metadata.type != vfs::FileType::kSymlink) {
    return absl::InvalidArgumentError(absl::StrCat("not a symlink: ", *key));
  }
  return node->second.link_target;
}

absl::StatusOr<std::string> AsarFileSystem::FsType(std::string_view /*path*/) const {
  return "asar";
}

absl::StatusOr<bool> AsarFileSystem::IsCaseSensitive(std::string_view /*path*/) const {
  return true;
}

absl::StatusOr<std::string> AsarFileSystem::ReadNodeContent(std::string_view key, const Node& node) const {
  std::string content;
  if (node.unpacked) {
    if (!bytes_.empty()) {
      return absl::NotFoundError(absl::StrCat("nested ASAR has external unpacked member: ", key));
    }
    MBO_ASSIGN_OR_RETURN(
        content, ReadRange(absl::StrCat(container_, ".unpacked/", key), 0, node.metadata.size, /*require_end=*/true));
  } else if (!bytes_.empty()) {
    const std::uint64_t offset = data_offset_ + node.offset;
    content = bytes_.substr(static_cast<std::size_t>(offset), static_cast<std::size_t>(node.metadata.size));
  } else {
    MBO_ASSIGN_OR_RETURN(content, ReadRange(container_, data_offset_ + node.offset, node.metadata.size));
  }
  if (!node.integrity.has_value()) {
    return content;
  }
  if (Sha256(content) != node.integrity->hash) {
    return absl::DataLossError(absl::StrCat("ASAR integrity check failed for ", key));
  }
  for (std::size_t index = 0; index < node.integrity->blocks.size(); ++index) {
    const std::uint64_t begin = index * node.integrity->block_size;
    const std::uint64_t length = std::min<std::uint64_t>(node.integrity->block_size, content.size() - begin);
    if (Sha256(std::string_view(content).substr(static_cast<std::size_t>(begin), static_cast<std::size_t>(length)))
        != node.integrity->blocks[index]) {
      return absl::DataLossError(absl::StrCat("ASAR block integrity check failed for ", key, " block ", index));
    }
  }
  return content;
}

absl::StatusOr<std::string> AsarFileSystem::ReadContent(std::string_view path) const {
  const std::optional<std::string> key = MemberKeyOf(path);
  if (!key.has_value() || key->empty()) {
    return absl::FailedPreconditionError(absl::StrCat("not a regular ASAR member: ", path));
  }
  const auto node = nodes_.find(*key);
  if (node == nodes_.end()) {
    return absl::NotFoundError(absl::StrCat("no such ASAR member: ", *key));
  }
  if (node->second.metadata.type != vfs::FileType::kRegular) {
    return absl::FailedPreconditionError(absl::StrCat("not a regular ASAR member: ", *key));
  }
  {
    const absl::MutexLock lock(cache_mutex_);
    if (const auto cached = content_cache_.find(*key); cached != content_cache_.end()) {
      return cached->second;
    }
  }
  MBO_ASSIGN_OR_RETURN(std::string content, ReadNodeContent(*key, node->second));
  {
    const absl::MutexLock lock(cache_mutex_);
    content_cache_.emplace(*key, content);
  }
  return content;
}

}  // namespace xff::asar
