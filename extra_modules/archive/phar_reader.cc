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

#include "xff/archive/phar_reader.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "xff/archive/archive_reader.h"
#include "xff/archive/member_path.h"

namespace xff::archive {
namespace {

// The token that ends a phar's PHP stub. Everything after it (plus an optional ` ?>` and one line
// ending) is the binary manifest.
constexpr std::string_view kHaltToken = "__HALT_COMPILER();";

// How much of the stub is scanned for the halt token. PHP scans the whole file, but a stub is a
// short bootstrap script in practice, and an unbounded scan would turn every large non-phar file
// handed to the walk into a full read.
constexpr std::size_t kStubScanLimit = std::size_t{1} << 20U;

// A corrupt or hostile length field must not turn into a huge allocation, so the declared manifest
// length is capped. A manifest holds names and metadata only, never member data.
constexpr std::size_t kMaxManifestBytes = std::size_t{64} << 20U;

// Member flag bits (PHP's PHAR_ENT_* set). The low 9 bits are the POSIX permissions; a non-zero
// compression field means the member's stored bytes are deflate- or bzip2-compressed.
constexpr std::uint32_t kFlagPermissionMask = 0x000001FF;
constexpr std::uint32_t kFlagCompressionMask = 0x0000F000;
constexpr std::uint32_t kFlagCompressedGz = 0x00001000;
constexpr std::uint32_t kFlagCompressedBz2 = 0x00002000;

// A phar manifest entry, as stored, plus where its data sits in the container. Kept separate from
// the public `Member` because the offsets are an implementation detail of THIS format.
struct Entry {
  Member member;
  std::uint64_t data_offset = 0;  // absolute offset of the member's stored bytes in the container
  std::uint64_t stored_size = 0;  // bytes occupied there (== member.size unless compressed)
  std::uint32_t flags = 0;
};

// A cursor over the manifest bytes. Every read is bounds-checked and a short read is a DataLoss:
// the halt token already proved this is a phar, so a truncated manifest is a corrupt container, not
// "some other file".
class Cursor {
 public:
  explicit Cursor(std::string_view bytes) : bytes_(bytes) {}

  [[nodiscard]] absl::StatusOr<std::uint32_t> Uint32(std::string_view field) {
    if (at_ + 4 > bytes_.size()) {
      return Truncated(field);
    }
    // Little-endian, matching PHP's phar_get_32.
    const std::uint32_t value = Byte(0) | Byte(1) << 8U | Byte(2) << 16U | Byte(3) << 24U;
    at_ += 4;
    return value;
  }

  [[nodiscard]] absl::StatusOr<std::uint32_t> Uint16BigEndian(std::string_view field) {
    if (at_ + 2 > bytes_.size()) {
      return Truncated(field);
    }
    const std::uint32_t value = Byte(0) << 8U | Byte(1);
    at_ += 2;
    return value;
  }

  [[nodiscard]] absl::StatusOr<std::string_view> Bytes(std::uint32_t length, std::string_view field) {
    if (at_ + length > bytes_.size()) {
      return Truncated(field);
    }
    const std::string_view value = bytes_.substr(at_, length);
    at_ += length;
    return value;
  }

  // Reads a `[4 byte length][payload]` pair, the shape every variable-length manifest field uses.
  [[nodiscard]] absl::StatusOr<std::string_view> LengthPrefixed(std::string_view field) {
    absl::StatusOr<std::uint32_t> length = Uint32(absl::StrCat(field, " length"));
    if (!length.ok()) {
      return length.status();
    }
    return Bytes(*length, field);
  }

 private:
  // The byte `offset` past the cursor, widened UNSIGNED: `char` is signed on most targets, so a
  // plain widening of a byte over 0x7F would sign-extend and corrupt the value it is part of.
  [[nodiscard]] std::uint32_t Byte(std::size_t offset) const {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes_[at_ + offset]));
  }

  [[nodiscard]] static absl::Status Truncated(std::string_view field) {
    return absl::DataLossError(absl::StrCat("phar manifest is truncated: no room for the ", field));
  }

  std::string_view bytes_;
  std::size_t at_ = 0;
};

// The offset at which the manifest begins, or nullopt when `prefix` holds no halt token. The token
// may be followed by an optional ` ?>` and one line ending, all of which belong to the stub.
std::optional<std::size_t> ManifestOffset(std::string_view prefix) {
  const std::string_view::size_type token = prefix.find(kHaltToken);
  if (token == std::string_view::npos) {
    return std::nullopt;
  }
  std::size_t at = token + kHaltToken.size();
  while (at < prefix.size() && (prefix[at] == ' ' || prefix[at] == '\t')) {
    ++at;
  }
  if (prefix.substr(at).starts_with("?>")) {
    at += 2;
  }
  if (prefix.substr(at).starts_with("\r\n")) {
    at += 2;
  } else if (prefix.substr(at).starts_with("\n")) {
    ++at;
  }
  return at;
}

// Parses the manifest region (everything after the 4-byte manifest length), turning each entry into
// a `Member` plus the offsets its data occupies. `data_offset` is where the data section starts,
// which the caller knows and this function does not.
absl::StatusOr<std::vector<Entry>> ParseManifest(std::string_view manifest, std::uint64_t data_offset) {
  Cursor cursor(manifest);
  absl::StatusOr<std::uint32_t> count = cursor.Uint32("member count");
  if (!count.ok()) {
    return count.status();
  }
  // The manifest API version, nibble-packed big-endian (0x11 0x10 is 1.1.1). Every version so far
  // shares this layout, so it is consumed rather than branched on - but it must be consumed, or every
  // field after it is read two bytes off.
  absl::StatusOr<std::uint32_t> api_version = cursor.Uint16BigEndian("API version");
  if (!api_version.ok()) {
    return api_version.status();
  }
  // The global flags carry a container-wide compression hint and a "signature present" bit. Neither
  // changes how members are located, so they are read past rather than acted on here.
  absl::StatusOr<std::uint32_t> global_flags = cursor.Uint32("global flags");
  if (!global_flags.ok()) {
    return global_flags.status();
  }
  // The container's own alias (`phar://app.phar/...` names it). Not needed to locate members, but
  // its length field must be honoured for the same reason.
  absl::StatusOr<std::string_view> alias = cursor.LengthPrefixed("alias");
  if (!alias.ok()) {
    return alias.status();
  }
  absl::StatusOr<std::string_view> metadata = cursor.LengthPrefixed("container metadata");
  if (!metadata.ok()) {
    return metadata.status();
  }

  std::vector<Entry> entries;
  entries.reserve(*count);
  std::uint64_t next_data_offset = data_offset;
  for (std::uint32_t index = 0; index < *count; ++index) {
    absl::StatusOr<std::string_view> name = cursor.LengthPrefixed("member name");
    if (!name.ok()) {
      return name.status();
    }
    absl::StatusOr<std::uint32_t> size = cursor.Uint32("member size");
    if (!size.ok()) {
      return size.status();
    }
    absl::StatusOr<std::uint32_t> mtime = cursor.Uint32("member timestamp");
    if (!mtime.ok()) {
      return mtime.status();
    }
    absl::StatusOr<std::uint32_t> stored_size = cursor.Uint32("member stored size");
    if (!stored_size.ok()) {
      return stored_size.status();
    }
    // The CRC32 of the uncompressed content. Verifying it needs the content, so it belongs with the
    // read path; this slice reads past it rather than storing an unused field.
    absl::StatusOr<std::uint32_t> crc32 = cursor.Uint32("member CRC32");
    if (!crc32.ok()) {
      return crc32.status();
    }
    absl::StatusOr<std::uint32_t> flags = cursor.Uint32("member flags");
    if (!flags.ok()) {
      return flags.status();
    }
    absl::StatusOr<std::string_view> member_metadata = cursor.LengthPrefixed("member metadata");
    if (!member_metadata.ok()) {
      return member_metadata.status();
    }
    if (name->empty()) {
      return absl::DataLossError("phar manifest holds a member with an empty name");
    }
    // A trailing `/` is how phar spells a directory: there is no flag bit for it.
    const bool is_directory = name->ends_with('/');
    Entry entry{
        .member{
            .path = std::string(is_directory ? name->substr(0, name->size() - 1) : *name),
            .size = static_cast<std::int64_t>(*size),
            .mtime = static_cast<std::int64_t>(*mtime),
            .mode = *flags & kFlagPermissionMask,
            .is_directory = is_directory,
            // phar has no symlink member type: the format stores file data and directories only.
            .is_symlink = false,
        },
        .data_offset = next_data_offset,
        .stored_size = *stored_size,
        .flags = *flags,
    };
    next_data_offset += *stored_size;
    entries.push_back(std::move(entry));
  }
  return entries;
}

// Splits `bytes` into the manifest region and the absolute offset of the data section, so both entry
// points share one notion of "where the manifest is".
absl::StatusOr<std::vector<Entry>> ParseWholePhar(std::string_view bytes) {
  const std::optional<std::size_t> manifest_at = ManifestOffset(bytes.substr(0, kStubScanLimit));
  if (!manifest_at.has_value()) {
    return absl::InvalidArgumentError("not a phar: no __HALT_COMPILER(); token in the stub");
  }
  Cursor length_cursor(bytes.substr(*manifest_at));
  absl::StatusOr<std::uint32_t> manifest_length = length_cursor.Uint32("manifest length");
  if (!manifest_length.ok()) {
    // The token is there but not even the length field fits, so this is a phar-shaped truncation.
    return manifest_length.status();
  }
  const std::size_t manifest_start = *manifest_at + 4;
  if (manifest_start + *manifest_length > bytes.size()) {
    return absl::DataLossError(
        absl::StrCat(
            "phar manifest is truncated: declares ", *manifest_length, " bytes, only ", bytes.size() - manifest_start,
            " remain"));
  }
  return ParseManifest(bytes.substr(manifest_start, *manifest_length), manifest_start + *manifest_length);
}

std::vector<Member> MembersOf(std::vector<Entry> entries) {
  std::vector<Member> members;
  members.reserve(entries.size());
  for (Entry& entry : entries) {
    members.push_back(std::move(entry.member));
  }
  return members;
}

// Locates `member` in an already-parsed manifest, normalizing both sides so the stored spelling and
// the caller's spelling never have to agree exactly.
absl::StatusOr<const Entry*> FindEntry(const std::vector<Entry>& entries, std::string_view member) {
  const std::string_view wanted = NormalizeMemberName(member);
  for (const Entry& entry : entries) {
    if (NormalizeMemberName(entry.member.path) != wanted) {
      continue;
    }
    if (entry.member.is_directory) {
      return absl::FailedPreconditionError(absl::StrCat("phar member is a directory and has no content: ", member));
    }
    if ((entry.flags & kFlagCompressionMask) != 0) {
      const std::string_view how = (entry.flags & kFlagCompressedGz) != 0    ? "deflate"
                                   : (entry.flags & kFlagCompressedBz2) != 0 ? "bzip2"
                                                                             : "an unknown method";
      return absl::UnimplementedError(
          absl::StrCat("phar member is compressed with ", how, ", which this reader cannot yet decompress: ", member));
    }
    return &entry;
  }
  return absl::NotFoundError(absl::StrCat("no such phar member: ", member));
}

absl::Status CheckLimit(std::uint64_t size, std::uint64_t max_bytes, std::string_view member) {
  if (max_bytes != 0 && size > max_bytes) {
    return absl::ResourceExhaustedError(
        absl::StrCat("phar member ", member, " is ", size, " bytes, over the ", max_bytes, " byte limit"));
  }
  return absl::OkStatus();
}

// Reads `length` bytes at `offset` from `path`. Used for both the stub-plus-manifest prefix and a
// single member's byte range, so neither entry point ever reads a whole container.
absl::StatusOr<std::string> ReadFileRange(std::string_view path, std::uint64_t offset, std::size_t length) {
  std::ifstream file(std::string(path), std::ios::binary);
  if (!file.is_open()) {
    return absl::InvalidArgumentError(absl::StrCat("cannot open ", path));
  }
  file.seekg(static_cast<std::streamoff>(offset));
  if (!file.good()) {
    return absl::DataLossError(absl::StrCat("cannot seek to offset ", offset, " in ", path));
  }
  std::string buffer(length, '\0');
  file.read(buffer.data(), static_cast<std::streamsize>(length));
  buffer.resize(static_cast<std::size_t>(file.gcount()));
  return buffer;
}

// The stub-plus-manifest prefix of the phar file at `path`: enough to parse, never the data section.
// Read in two steps because the manifest length is itself in the file - first the scan window, then
// exactly as much more as the manifest declares.
absl::StatusOr<std::string> ReadHeader(std::string_view path) {
  absl::StatusOr<std::string> prefix = ReadFileRange(path, 0, kStubScanLimit);
  if (!prefix.ok()) {
    return prefix;
  }
  const std::optional<std::size_t> manifest_at = ManifestOffset(*prefix);
  if (!manifest_at.has_value()) {
    return absl::InvalidArgumentError(
        absl::StrCat("not a phar: no __HALT_COMPILER(); token in the first ", kStubScanLimit, " bytes of ", path));
  }
  Cursor cursor(std::string_view(*prefix).substr(*manifest_at));
  absl::StatusOr<std::uint32_t> manifest_length = cursor.Uint32("manifest length");
  if (!manifest_length.ok()) {
    return manifest_length.status();
  }
  if (*manifest_length > kMaxManifestBytes) {
    return absl::DataLossError(
        absl::StrCat("phar manifest declares ", *manifest_length, " bytes, over the ", kMaxManifestBytes, " cap"));
  }
  const std::size_t needed = *manifest_at + 4 + *manifest_length;
  if (needed <= prefix->size()) {
    return prefix;
  }
  return ReadFileRange(path, 0, needed);
}

}  // namespace

absl::StatusOr<std::vector<Member>> ListPharMembers(std::string_view bytes) {
  absl::StatusOr<std::vector<Entry>> manifest = ParseWholePhar(bytes);
  if (!manifest.ok()) {
    return manifest.status();
  }
  return MembersOf(*std::move(manifest));
}

absl::StatusOr<std::vector<Member>> ListPharMembersOfFile(std::string_view path) {
  absl::StatusOr<std::string> header = ReadHeader(path);
  if (!header.ok()) {
    return header.status();
  }
  return ListPharMembers(*header);
}

absl::StatusOr<std::string> ReadPharMember(std::string_view bytes, std::string_view member, std::uint64_t max_bytes) {
  absl::StatusOr<std::vector<Entry>> manifest = ParseWholePhar(bytes);
  if (!manifest.ok()) {
    return manifest.status();
  }
  absl::StatusOr<const Entry*> entry = FindEntry(*manifest, member);
  if (!entry.ok()) {
    return entry.status();
  }
  const absl::Status limit = CheckLimit((*entry)->stored_size, max_bytes, member);
  if (!limit.ok()) {
    return limit;
  }
  if ((*entry)->data_offset + (*entry)->stored_size > bytes.size()) {
    return absl::DataLossError(absl::StrCat("phar member data runs past the end of the container: ", member));
  }
  return std::string(bytes.substr((*entry)->data_offset, (*entry)->stored_size));
}

absl::StatusOr<std::string> ReadPharMemberOfFile(
    std::string_view path,
    std::string_view member,
    std::uint64_t max_bytes) {
  absl::StatusOr<std::string> header = ReadHeader(path);
  if (!header.ok()) {
    return header.status();
  }
  absl::StatusOr<std::vector<Entry>> manifest = ParseWholePhar(*header);
  if (!manifest.ok()) {
    return manifest.status();
  }
  absl::StatusOr<const Entry*> entry = FindEntry(*manifest, member);
  if (!entry.ok()) {
    return entry.status();
  }
  const absl::Status limit = CheckLimit((*entry)->stored_size, max_bytes, member);
  if (!limit.ok()) {
    return limit;
  }
  absl::StatusOr<std::string> content =
      ReadFileRange(path, (*entry)->data_offset, static_cast<std::size_t>((*entry)->stored_size));
  if (!content.ok()) {
    return content;
  }
  if (content->size() < (*entry)->stored_size) {
    return absl::DataLossError(absl::StrCat("phar member data runs past the end of the container: ", member));
  }
  return content;
}

}  // namespace xff::archive
