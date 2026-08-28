// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/squashfs/squashfs_reader.h"

#include <sqsh.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "mbo/status/status_macros.h"
#include "xff/archive/member_path.h"
#include "xff/vfs/entry.h"

namespace xff::squashfs {
namespace {

struct ArchiveCloser {
  // XFF_ABI_POINTER: libsqsh's close function accepts its opaque archive handle.
  void operator()(struct SqshArchive* archive) const { sqsh_archive_close(archive); }
};

struct FileCloser {
  // XFF_ABI_POINTER: libsqsh's close function accepts its opaque file handle.
  void operator()(struct SqshFile* file) const { sqsh_close(file); }
};

using ArchivePtr = std::unique_ptr<struct SqshArchive, ArchiveCloser>;
using FilePtr = std::unique_ptr<struct SqshFile, FileCloser>;

constexpr std::string_view kMagic = "hsqs";
constexpr std::uint64_t kVirtualDeviceBit = std::uint64_t{1} << 63U;

struct MemberRecord {
  std::string path;
  std::uint64_t size = 0;
  std::int64_t mtime = 0;
  std::uint32_t mode = 0;
  vfs::FileType type = vfs::FileType::kUnknown;
  std::string link_target;
};

absl::Status ErrorStatus(int error, std::string_view operation) {
  const int code = error < 0 ? -error : error;
  const std::string message = absl::StrCat(operation, ": ", sqsh_error_str(error));
  if (code == SQSH_ERROR_WRONG_MAGIC || code == SQSH_ERROR_SUPERBLOCK_TOO_SMALL) {
    return absl::InvalidArgumentError(message);
  }
  if (code == ENOENT || code == SQSH_ERROR_NO_SUCH_FILE) {
    return absl::NotFoundError(message);
  }
  if (code == SQSH_ERROR_NOT_A_FILE || code == SQSH_ERROR_NOT_A_DIRECTORY || code == SQSH_ERROR_NOT_A_SYMLINK) {
    return absl::FailedPreconditionError(message);
  }
  if (code == ENOMEM || code == SQSH_ERROR_MALLOC_FAILED) {
    return absl::ResourceExhaustedError(message);
  }
  return absl::DataLossError(message);
}

ArchivePtr OpenAt(const void* source, std::optional<std::size_t> size, std::uint64_t offset, int& error) {
  struct SqshConfig config = {};
  config.archive_offset = offset;
  if (size.has_value()) {
    config.source_mapper = sqsh_mapper_impl_static;
    config.source_size = *size;
  }
  return ArchivePtr(sqsh_archive_open(source, &config, &error));
}

absl::StatusOr<ArchivePtr> OpenAppImageBytes(std::string_view container, std::string_view bytes) {
  for (std::string_view::size_type offset = bytes.find(kMagic); offset != std::string_view::npos;
       offset = bytes.find(kMagic, offset + 1)) {
    int error = 0;
    ArchivePtr archive = OpenAt(bytes.data(), bytes.size(), offset, error);
    if (archive != nullptr) {
      return archive;
    }
  }
  return absl::InvalidArgumentError(absl::StrCat("cannot find a SquashFS payload in AppImage ", container));
}

absl::StatusOr<ArchivePtr> OpenAppImagePath(std::string_view container, std::string& path_storage) {
  path_storage = container;
  std::ifstream input(path_storage, std::ios::binary);
  if (!input) {
    return absl::NotFoundError(absl::StrCat("cannot open AppImage ", container));
  }
  std::array<char, std::size_t{64} * 1'024> block = {};
  std::string overlap;
  std::uint64_t consumed = 0;
  while (input) {
    input.read(block.data(), static_cast<std::streamsize>(block.size()));
    const std::string chunk = overlap + std::string(block.data(), static_cast<std::size_t>(input.gcount()));
    for (std::string::size_type found = chunk.find(kMagic); found != std::string::npos;
         found = chunk.find(kMagic, found + 1)) {
      const std::uint64_t offset = consumed - overlap.size() + found;
      int error = 0;
      ArchivePtr archive = OpenAt(path_storage.c_str(), std::nullopt, offset, error);
      if (archive != nullptr) {
        return archive;
      }
    }
    consumed += static_cast<std::uint64_t>(input.gcount());
    overlap = chunk.substr(chunk.size() > kMagic.size() - 1 ? chunk.size() - (kMagic.size() - 1) : 0);
  }
  return absl::InvalidArgumentError(absl::StrCat("cannot find a SquashFS payload in AppImage ", container));
}

absl::StatusOr<ArchivePtr> OpenArchive(
    std::string_view container,
    std::optional<std::string_view> bytes,
    std::string& path_storage) {
  if (absl::EndsWithIgnoreCase(container, ".appimage")) {
    return bytes.has_value() ? OpenAppImageBytes(container, *bytes) : OpenAppImagePath(container, path_storage);
  }
  int error = 0;
  ArchivePtr archive;
  if (bytes.has_value()) {
    archive = OpenAt(bytes->data(), bytes->size(), /*offset=*/0, error);
  } else {
    path_storage = container;
    archive = OpenAt(path_storage.c_str(), std::nullopt, /*offset=*/0, error);
  }
  if (archive == nullptr) {
    return ErrorStatus(error, absl::StrCat("cannot open SquashFS container ", container));
  }
  return archive;
}

std::string SquashPath(std::string_view member) {
  return member.starts_with('/') ? std::string(member) : absl::StrCat("/", member);
}

vfs::FileType FileType(enum SqshFileType type) {
  switch (type) {
    case SQSH_FILE_TYPE_DIRECTORY: return vfs::FileType::kDirectory;
    case SQSH_FILE_TYPE_FILE: return vfs::FileType::kRegular;
    case SQSH_FILE_TYPE_SYMLINK: return vfs::FileType::kSymlink;
    case SQSH_FILE_TYPE_BLOCK: return vfs::FileType::kBlockDevice;
    case SQSH_FILE_TYPE_CHAR: return vfs::FileType::kCharDevice;
    case SQSH_FILE_TYPE_FIFO: return vfs::FileType::kFifo;
    case SQSH_FILE_TYPE_SOCKET: return vfs::FileType::kSocket;
  }
  return vfs::FileType::kUnknown;
}

absl::StatusOr<FilePtr> OpenFile(struct SqshArchive* archive, std::string_view path) {
  const std::string squash_path = SquashPath(path);
  int error = 0;
  FilePtr file(sqsh_lopen(archive, squash_path.c_str(), &error));
  if (file == nullptr) {
    return ErrorStatus(error, absl::StrCat("cannot read SquashFS member ", path));
  }
  return file;
}

absl::StatusOr<std::vector<MemberRecord>> ListMembers(
    std::string_view container,
    std::optional<std::string_view> bytes) {
  std::string path_storage;
  MBO_ASSIGN_OR_RETURN(const ArchivePtr archive, OpenArchive(container, bytes, path_storage));
  int error = 0;
  const std::unique_ptr<char*, decltype(&std::free)> paths(
      sqsh_easy_tree_traversal(archive.get(), "/", &error), &std::free);
  if (paths == nullptr) {
    return ErrorStatus(error, absl::StrCat("cannot list SquashFS container ", container));
  }

  std::vector<MemberRecord> members;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): libsqsh returns a null-terminated char**.
  for (char* const * current = paths.get(); *current != nullptr; ++current) {
    std::string_view path(*current);
    MBO_ASSIGN_OR_RETURN(const FilePtr file, OpenFile(archive.get(), path));
    const vfs::FileType type = FileType(sqsh_file_type(file.get()));
    if (path.starts_with('/')) {
      path.remove_prefix(1);
    }
    MemberRecord member{
        .path = std::string(path),
        .size = sqsh_file_size(file.get()),
        .mtime = sqsh_file_modified_time(file.get()),
        .mode = sqsh_file_permission(file.get()),
        .type = type,
    };
    if (member.type == vfs::FileType::kSymlink) {
      member.link_target.assign(sqsh_file_symlink(file.get()), sqsh_file_symlink_size(file.get()));
    }
    members.push_back(std::move(member));
  }
  return members;
}

absl::StatusOr<std::string> ReadMember(
    std::string_view container,
    std::optional<std::string_view> bytes,
    std::string_view member) {
  std::string path_storage;
  MBO_ASSIGN_OR_RETURN(const ArchivePtr archive, OpenArchive(container, bytes, path_storage));
  MBO_ASSIGN_OR_RETURN(const FilePtr file, OpenFile(archive.get(), member));
  if (sqsh_file_type(file.get()) != SQSH_FILE_TYPE_FILE) {
    return absl::FailedPreconditionError(absl::StrCat("not a regular SquashFS member: ", member));
  }
  const std::uint64_t size = sqsh_file_size(file.get());
  if (!std::in_range<std::size_t>(size)) {
    return absl::ResourceExhaustedError(absl::StrCat("SquashFS member exceeds read limit: ", member));
  }
  const std::string squash_path = SquashPath(member);
  int error = 0;
  const std::unique_ptr<std::uint8_t, decltype(&std::free)> content(
      sqsh_easy_file_content(archive.get(), squash_path.c_str(), &error), &std::free);
  if (content == nullptr) {
    return ErrorStatus(error, absl::StrCat("cannot extract SquashFS member ", member));
  }
  std::string result(static_cast<std::size_t>(size), '\0');
  std::memcpy(result.data(), content.get(), result.size());
  return result;
}

std::string_view ParentOf(std::string_view key) {
  const std::string_view::size_type slash = key.rfind('/');
  return slash == std::string_view::npos ? std::string_view{} : key.substr(0, slash);
}

std::string_view NameOf(std::string_view key) {
  const std::string_view::size_type slash = key.rfind('/');
  return slash == std::string_view::npos ? key : key.substr(slash + 1);
}

vfs::Metadata RootMetadata() {
  return vfs::Metadata{
      .type = vfs::FileType::kDirectory,
      .source = vfs::Source::kArchiveMember,
      .mode = 0555,
      .nlink = 1,
  };
}

}  // namespace

absl::StatusOr<SquashfsFileSystem> SquashfsFileSystem::Open(
    std::string_view container,
    archive::MemberPathOptions options) {
  return Build(std::string(container), std::string(), /*memory_backed=*/false, options);
}

absl::StatusOr<SquashfsFileSystem> SquashfsFileSystem::OpenBytes(
    std::string_view container,
    std::string bytes,
    archive::MemberPathOptions options) {
  return Build(std::string(container), std::move(bytes), /*memory_backed=*/true, options);
}

absl::StatusOr<SquashfsFileSystem> SquashfsFileSystem::Build(
    std::string container,
    std::string bytes,
    bool memory_backed,
    archive::MemberPathOptions options) {
  const std::optional<std::string_view> source = memory_backed ? std::optional<std::string_view>{bytes} : std::nullopt;
  MBO_ASSIGN_OR_RETURN(const std::vector<MemberRecord> members, ListMembers(container, source));
  SquashfsFileSystem fs(std::move(container), options);
  fs.bytes_ = std::move(bytes);
  fs.memory_backed_ = memory_backed;
  const std::uint64_t device = kVirtualDeviceBit | absl::HashOf(fs.container_);
  std::uint64_t inode = 1;
  for (const MemberRecord& member : members) {
    Node node{
        .metadata =
            {
                .type = member.type,
                .source = vfs::Source::kArchiveMember,
                .size = member.size,
                .blocks = (member.size + 511U) / 512U,
                .mode = member.mode,
                .nlink = 1,
                .ino = inode++,
                .dev = device,
                .atime = absl::UnixEpoch(),
                .mtime = absl::FromUnixSeconds(member.mtime),
                .ctime = absl::FromUnixSeconds(member.mtime),
            },
        .link_target = member.link_target,
    };
    fs.nodes_.emplace(member.path, std::move(node));
  }
  return fs;
}

std::optional<std::string> SquashfsFileSystem::MemberKeyOf(std::string_view path) const {
  if (path == container_) {
    return std::string();
  }
  const std::optional<archive::MemberPathParts> parts =
      archive::SplitMemberPath(path, options_, [this](std::string_view candidate) { return candidate == container_; });
  if (!parts.has_value()) {
    return std::nullopt;
  }
  std::string_view member = archive::NormalizeMemberName(parts->member);
  if (member.starts_with('/')) {
    member.remove_prefix(1);
  }
  return std::string(member);
}

absl::StatusOr<std::vector<vfs::Entry>> SquashfsFileSystem::ReadDir(std::string_view dir) const {
  const std::optional<std::string> key = MemberKeyOf(dir);
  if (!key.has_value()) {
    return absl::InvalidArgumentError(absl::StrCat("not a path in ", container_, ": ", dir));
  }
  if (!key->empty()) {
    const auto found = nodes_.find(*key);
    if (found == nodes_.end()) {
      return absl::NotFoundError(absl::StrCat("no such SquashFS member: ", *key));
    }
    if (found->second.metadata.type != vfs::FileType::kDirectory) {
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

absl::StatusOr<vfs::Metadata> SquashfsFileSystem::Stat(std::string_view path, bool /*follow_symlinks*/) const {
  const std::optional<std::string> key = MemberKeyOf(path);
  if (!key.has_value()) {
    return absl::InvalidArgumentError(absl::StrCat("not a path in ", container_, ": ", path));
  }
  if (key->empty()) {
    return RootMetadata();
  }
  const auto found = nodes_.find(*key);
  return found == nodes_.end()
             ? absl::StatusOr<vfs::Metadata>(absl::NotFoundError(absl::StrCat("no such SquashFS member: ", *key)))
             : absl::StatusOr<vfs::Metadata>(found->second.metadata);
}

absl::Status SquashfsFileSystem::Remove(std::string_view path) const {
  return absl::PermissionDeniedError(absl::StrCat("SquashFS members are read-only, cannot remove: ", path));
}

bool SquashfsFileSystem::Access(std::string_view path, vfs::AccessMode mode) const {
  if (mode == vfs::AccessMode::kWrite) {
    return false;
  }
  const absl::StatusOr<vfs::Metadata> metadata = Stat(path, /*follow_symlinks=*/false);
  if (!metadata.ok()) {
    return false;
  }
  return mode == vfs::AccessMode::kRead ? (metadata->mode & 0444U) != 0 : (metadata->mode & 0111U) != 0;
}

absl::StatusOr<std::string> SquashfsFileSystem::ReadLink(std::string_view path) const {
  const std::optional<std::string> key = MemberKeyOf(path);
  if (!key.has_value() || key->empty()) {
    return absl::InvalidArgumentError(absl::StrCat("not a SquashFS member path: ", path));
  }
  const auto found = nodes_.find(*key);
  if (found == nodes_.end()) {
    return absl::NotFoundError(absl::StrCat("no such SquashFS member: ", *key));
  }
  if (found->second.metadata.type != vfs::FileType::kSymlink) {
    return absl::InvalidArgumentError(absl::StrCat("not a symlink: ", *key));
  }
  return found->second.link_target;
}

absl::StatusOr<std::string> SquashfsFileSystem::FsType(std::string_view /*path*/) const {
  return std::string("squashfs");
}

absl::StatusOr<bool> SquashfsFileSystem::IsCaseSensitive(std::string_view /*path*/) const {
  return true;
}

absl::StatusOr<std::string> SquashfsFileSystem::ReadContent(std::string_view path) const {
  const std::optional<std::string> key = MemberKeyOf(path);
  if (!key.has_value() || key->empty()) {
    return absl::InvalidArgumentError(absl::StrCat("not a SquashFS member path: ", path));
  }
  const auto found = nodes_.find(*key);
  if (found == nodes_.end()) {
    return absl::NotFoundError(absl::StrCat("no such SquashFS member: ", *key));
  }
  if (found->second.metadata.type != vfs::FileType::kRegular) {
    return absl::FailedPreconditionError(absl::StrCat("not a regular file: ", *key));
  }
  const std::optional<std::string_view> source =
      memory_backed_ ? std::optional<std::string_view>{bytes_} : std::nullopt;
  return ReadMember(container_, source, *key);
}

}  // namespace xff::squashfs
