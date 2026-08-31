// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_SQUASHFS_SQUASHFS_READER_H_
#define XFF_SQUASHFS_SQUASHFS_READER_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xff/archive/member_path.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::squashfs {

// A read-only VFS over one SquashFS image. The image may be a filesystem path or retained bytes
// from a containing archive, which keeps nested traversal independent of the archive implementation.
class SquashfsFileSystem : public vfs::FileSystem {
 public:
  static absl::StatusOr<SquashfsFileSystem> Open(std::string_view container, archive::MemberPathOptions options = {});
  static absl::StatusOr<SquashfsFileSystem> OpenBytes(
      std::string_view container,
      std::string bytes,
      archive::MemberPathOptions options = {});

  [[nodiscard]] absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view dir) const override;
  [[nodiscard]] absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool follow_symlinks) const override;
  [[nodiscard]] absl::Status Remove(std::string_view path) const override;
  [[nodiscard]] bool Access(std::string_view path, vfs::AccessMode mode) const override;
  [[nodiscard]] absl::StatusOr<std::string> ReadLink(std::string_view path) const override;
  [[nodiscard]] absl::StatusOr<std::string> FsType(std::string_view path) const override;
  [[nodiscard]] absl::StatusOr<bool> IsCaseSensitive(std::string_view path) const override;
  [[nodiscard]] absl::StatusOr<std::string> ReadContent(std::string_view path) const override;

 private:
  struct Node {
    vfs::Metadata metadata;
    std::string link_target;
  };

  SquashfsFileSystem(std::string container, archive::MemberPathOptions options)
      : container_(std::move(container)), options_(options) {}

  static absl::StatusOr<SquashfsFileSystem> Build(
      std::string container,
      std::string bytes,
      bool memory_backed,
      archive::MemberPathOptions options);
  [[nodiscard]] std::optional<std::string> MemberKeyOf(std::string_view path) const;

  std::string container_;
  std::string bytes_;
  bool memory_backed_ = false;
  archive::MemberPathOptions options_;
  std::map<std::string, Node, std::less<>> nodes_;
};

}  // namespace xff::squashfs

#endif  // XFF_SQUASHFS_SQUASHFS_READER_H_
