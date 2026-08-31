// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
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

#include <bzlib.h>
#include <zlib.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "mbo/status/status_macros.h"
#include "xff/archive/archive_reader.h"
#include "xff/archive/member_path.h"

namespace xff::archive {

namespace {

constexpr std::string_view kPharStubMember = ".phar/stub.php";

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
  // This member's own manifest entry, as a byte range RELATIVE to the manifest region. Only a writer
  // needs it (removing a member copies every other entry through verbatim), but it is free to record
  // while parsing and impossible to recover afterwards.
  std::size_t entry_offset = 0;
  std::size_t entry_size = 0;
  std::string stored_name;  // the name as stored, so a directory keeps its trailing `/`
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

  // The cursor's position, which a writer needs to record where each manifest entry begins and ends.
  [[nodiscard]] std::size_t At() const { return at_; }

  // Advances past `length` bytes, or returns false when they are not there.
  [[nodiscard]] bool Skip(std::uint32_t length) {
    if (at_ + length > bytes_.size()) {
      return false;
    }
    at_ += length;
    return true;
  }

  // Reads a `[4 byte length][payload]` pair, the shape every variable-length manifest field uses.
  [[nodiscard]] absl::StatusOr<std::string_view> LengthPrefixed(std::string_view field) {
    MBO_ASSIGN_OR_RETURN(const std::uint32_t length, Uint32(absl::StrCat(field, " length")));
    return Bytes(length, field);
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

// The manifest's fixed header: member count (4), API version (2), global flags (4), and the length
// fields of the alias and the container metadata (4 each). The smallest possible manifest is exactly
// this, with both variable parts empty.
constexpr std::size_t kMinManifestBytes = 18;

// The smallest a member entry can be: a one-character name (4 + 1), five 4-byte fields, and a
// metadata length of 0. Used to reject a member count that the declared manifest could not possibly
// hold, which is what tells a real phar apart from a file that merely contains the halt token.
constexpr std::size_t kMinEntryBytes = 4 + 1 + (5 * 4) + 4;

// Where the manifest starts, given the offset just past a halt token.
//
// This follows PHP exactly, which is the specification in practice. From php-src
// ext/phar/phar.c, in `phar_parse_pharfile` (around line 764) - NOT in `phar_open_from_fp`, which only
// locates the token and passes the offset down:
//
//     if ((*buffer == ' ' || *buffer == '\n') && *(buffer + 1) == '?' && *(buffer + 2) == '>') {
//         halt_offset += 3;   /* then an optional \n, or \r\n */
//
// So the close tag is consumed ONLY as the three-byte sequence `" ?>"` or `"\n?>"`: exactly one space
// or exactly one newline, then `?>`, then at most one line ending. A bare `?>` with no separator, a
// tab, two spaces, a trailing space after `?>`, `\r\n?>`, or a lone line ending are all NOT skipped -
// the manifest is then taken to start immediately after the `;`, which is itself the legal minimal
// spelling, and the manifest-header check below decides whether that was right.
//
// (One deliberate difference: after `?>` PHP treats a `\r` not followed by `\n` as corruption, while
// here it simply leaves the offset unskipped, so such a file reports "not a phar" rather than
// "corrupt". Our contract prefers that for anything whose manifest does not check out.)
std::size_t SkipStubTail(std::string_view bytes, std::size_t at) {
  const std::string_view tail = bytes.substr(at);
  if (!tail.starts_with(" ?>") && !tail.starts_with("\n?>")) {
    // No marker: the manifest starts immediately after the `;`, the legal minimal spelling.
    return at;
  }
  at += 3;
  // PHP's line ending handling, mirrored: it reads ONE character, and its `\n` test is NOT an
  // `else if`, so the `\r` branch reassigns that character to `\n` and falls THROUGH to be counted
  // again - which is how `\r\n` ends up advancing by two. A `\r` not followed by `\n` is
  // "truncated manifest at stub end" to PHP (verified against it), where this leaves the offset at the
  // `\r` instead, so the manifest check below reports "not a phar" rather than "corrupt".
  const std::string_view after_marker = bytes.substr(at);
  if (after_marker.starts_with('\r')) {
    return after_marker.starts_with("\r\n") ? at + 2 : at;
  }
  if (after_marker.starts_with('\n')) {
    return at + 1;
  }
  return at;
}

// Whether a manifest plausibly starts at `at` (the 4-byte manifest length, then the header).
//
// This check is what makes the halt token INSUFFICIENT evidence on its own, and it is not
// hypothetical: a tar- or zip-based phar stores the stub as an ordinary member (`.phar/stub.php`), so
// the token appears inside a perfectly good tar. Committing on the token alone reported that tar as a
// CORRUPT phar - worse than not recognising it, because the walk reports an error instead of treating
// the file as the archive it is.
//
// Only the fixed header is required to be present, deliberately: if the header is plausible but the
// file ends inside the declared manifest, that is a truncated phar (DataLoss), not "some other file".
bool LooksLikeManifestAt(std::string_view bytes, std::size_t at) {
  if (at + 4 > bytes.size()) {
    return false;
  }
  Cursor cursor(bytes.substr(at));
  const absl::StatusOr<std::uint32_t> manifest_length = cursor.Uint32("manifest length");
  if (!manifest_length.ok() || *manifest_length < kMinManifestBytes || *manifest_length > kMaxManifestBytes) {
    return false;
  }
  const absl::StatusOr<std::uint32_t> count = cursor.Uint32("member count");
  if (!count.ok()) {
    return false;
  }
  // A count the declared manifest cannot hold means these bytes are not a manifest.
  if ((static_cast<std::uint64_t>(*count) * kMinEntryBytes) + kMinManifestBytes > *manifest_length) {
    return false;
  }
  const absl::StatusOr<std::uint32_t> api_version = cursor.Uint16BigEndian("API version");
  // The version is nibble-packed with a non-zero major (1.x.y to date), so a zero high nibble - what
  // tar's NUL padding after the stub member yields - is not a version.
  if (!api_version.ok() || (*api_version >> 12U) == 0) {
    return false;
  }
  const absl::StatusOr<std::uint32_t> global_flags = cursor.Uint32("global flags");
  if (!global_flags.ok()) {
    return false;
  }
  // The two variable-length header fields must fit inside the DECLARED manifest (not inside what
  // happens to be present, per the comment above).
  static constexpr std::array kHeaderFields = std::to_array<std::string_view>({
      "alias",
      "container metadata",
  });
  std::uint64_t used = kMinManifestBytes;
  for (const std::string_view field : kHeaderFields) {
    const absl::StatusOr<std::uint32_t> length = cursor.Uint32(field);
    if (!length.ok()) {
      return false;
    }
    used += *length;
    if (used > *manifest_length) {
      return false;
    }
    if (!cursor.Skip(*length)) {
      // Beyond what is present: the header itself is plausible, so let the parse decide.
      return true;
    }
  }
  return true;
}

// The offset at which the manifest begins, or nullopt when `bytes` holds no halt token FOLLOWED BY a
// plausible manifest. Every token occurrence is tried, since the first one can belong to a stub stored
// as a member of some other archive.
std::optional<std::size_t> ManifestOffset(std::string_view bytes) {
  for (std::string_view::size_type token = bytes.find(kHaltToken); token != std::string_view::npos;
       token = bytes.find(kHaltToken, token + 1)) {
    const std::size_t at = SkipStubTail(bytes, token + kHaltToken.size());
    if (LooksLikeManifestAt(bytes, at)) {
      return at;
    }
  }
  return std::nullopt;
}

// Parses the manifest region (everything after the 4-byte manifest length), turning each entry into
// a `Member` plus the offsets its data occupies. `data_offset` is where the data section starts,
// which the caller knows and this function does not.
// `header_size` reports how much of the manifest precedes the first member entry (the fixed header
// plus the alias and the container metadata), which a writer keeps verbatim.
// One member entry, read from `cursor` at its current position. Split out of ParseManifest because
// the manifest header and the per-member record are two different formats that happen to be adjacent;
// keeping them in one function also put it well past the complexity the style guide allows.
// `data_offset` is where this member's data starts, which the caller advances by the stored size.
absl::StatusOr<Entry> ParseEntry(Cursor& cursor, std::uint64_t data_offset) {
  const std::size_t entry_at = cursor.At();
  MBO_ASSIGN_OR_RETURN(const std::string_view name, cursor.LengthPrefixed("member name"));
  MBO_ASSIGN_OR_RETURN(const std::uint32_t size, cursor.Uint32("member size"));
  MBO_ASSIGN_OR_RETURN(const std::uint32_t mtime, cursor.Uint32("member timestamp"));
  MBO_ASSIGN_OR_RETURN(const std::uint32_t stored_size, cursor.Uint32("member stored size"));
  // The CRC32 of the uncompressed content. Verifying it needs the content, so it belongs with the
  // read path; this slice reads past it rather than storing an unused field.
  MBO_RETURN_IF_ERROR(cursor.Uint32("member CRC32"));
  MBO_ASSIGN_OR_RETURN(const std::uint32_t flags, cursor.Uint32("member flags"));
  MBO_RETURN_IF_ERROR(cursor.LengthPrefixed("member metadata"));
  if (name.empty()) {
    return absl::DataLossError("phar manifest holds a member with an empty name");
  }
  // A trailing `/` is how phar spells a directory: there is no flag bit for it.
  const bool is_directory = name.ends_with('/');
  return Entry{
      .member{
          .path = std::string(is_directory ? name.substr(0, name.size() - 1) : name),
          .size = static_cast<std::int64_t>(size),
          .mtime = static_cast<std::int64_t>(mtime),
          .mode = flags & kFlagPermissionMask,
          .is_directory = is_directory,
          // phar has no symlink member type: the format stores file data and directories only.
          .is_symlink = false,
      },
      .data_offset = data_offset,
      .stored_size = stored_size,
      .flags = flags,
      .entry_offset = entry_at,
      .entry_size = cursor.At() - entry_at,
      .stored_name = std::string(name),
  };
}

absl::StatusOr<std::vector<Entry>> ParseManifest(
    std::string_view manifest,
    std::uint64_t data_offset,
    std::size_t& header_size) {
  Cursor cursor(manifest);
  MBO_ASSIGN_OR_RETURN(const std::uint32_t count, cursor.Uint32("member count"));
  // The manifest API version, nibble-packed big-endian (0x11 0x10 is 1.1.1). Every version so far
  // shares this layout, so it is consumed rather than branched on - but it must be consumed, or every
  // field after it is read two bytes off.
  MBO_RETURN_IF_ERROR(cursor.Uint16BigEndian("API version"));
  // The global flags carry a container-wide compression hint and a "signature present" bit. Neither
  // changes how members are located, so they are read past rather than acted on here.
  MBO_RETURN_IF_ERROR(cursor.Uint32("global flags"));
  // The container's own alias (`phar://app.phar/...` names it). Not needed to locate members, but
  // its length field must be honoured for the same reason.
  MBO_RETURN_IF_ERROR(cursor.LengthPrefixed("alias"));
  MBO_RETURN_IF_ERROR(cursor.LengthPrefixed("container metadata"));

  std::vector<Entry> entries;
  entries.reserve(count);
  std::uint64_t next_data_offset = data_offset;
  const std::size_t entries_at = cursor.At();  // the fixed header, the alias and the metadata end here
  for (std::uint32_t index = 0; index < count; ++index) {
    MBO_ASSIGN_OR_RETURN(Entry entry, ParseEntry(cursor, next_data_offset));
    next_data_offset += entry.stored_size;
    entries.push_back(std::move(entry));
  }
  header_size = entries_at;
  return entries;
}

// Where the manifest is and what it holds, in one place, so every entry point (list, read, and the
// layout a writer needs) shares one notion of it.
struct WholePhar {
  std::vector<Entry> entries;
  std::size_t manifest_length_at = 0;
  std::size_t manifest_start = 0;
  std::size_t manifest_size = 0;
  std::size_t entries_offset = 0;  // absolute: where the first member entry begins
  std::uint32_t global_flags = 0;
};

absl::StatusOr<WholePhar> ParseWholePharLayout(std::string_view bytes) {
  const std::optional<std::size_t> manifest_at = ManifestOffset(bytes.substr(0, kStubScanLimit));
  if (!manifest_at.has_value()) {
    return absl::InvalidArgumentError("not a phar: no __HALT_COMPILER(); token in the stub");
  }
  Cursor length_cursor(bytes.substr(*manifest_at));
  // The token is there, so a length field that does not fit is a phar-shaped truncation, not "some
  // other file" - the macro propagates the reader's DataLoss unchanged.
  MBO_ASSIGN_OR_RETURN(const std::uint32_t manifest_length, length_cursor.Uint32("manifest length"));
  const std::size_t manifest_start = *manifest_at + 4;
  if (manifest_start + manifest_length > bytes.size()) {
    return absl::DataLossError(
        absl::StrCat(
            "phar manifest is truncated: declares ", manifest_length, " bytes, only ", bytes.size() - manifest_start,
            " remain"));
  }
  const std::string_view manifest = bytes.substr(manifest_start, manifest_length);
  std::size_t header_size = 0;
  MBO_ASSIGN_OR_RETURN(
      std::vector<Entry> entries, ParseManifest(manifest, manifest_start + manifest_length, header_size));
  // The global flags live right after the member count and the API version, and only a writer needs
  // them (the signature bit says whether one has to be recomputed).
  Cursor flags_cursor(manifest.substr(4 + 2));
  MBO_ASSIGN_OR_RETURN(const std::uint32_t global_flags, flags_cursor.Uint32("global flags"));
  return WholePhar{
      .entries = std::move(entries),
      .manifest_length_at = *manifest_at,
      .manifest_start = manifest_start,
      .manifest_size = manifest_length,
      .entries_offset = manifest_start + header_size,
      .global_flags = global_flags,
  };
}

std::vector<Member> MembersOf(std::vector<Entry> entries) {
  std::vector<Member> members;
  members.reserve(entries.size());
  for (Entry& entry : entries) {
    members.push_back(std::move(entry.member));
  }
  return members;
}

bool HasStoredStub(const std::vector<Entry>& entries) {
  return absl::c_any_of(
      entries, [](const Entry& entry) { return NormalizeMemberName(entry.member.path) == kPharStubMember; });
}

Member StubMember(std::size_t size) {
  return Member{
      .path = std::string(kPharStubMember),
      .size = static_cast<std::int64_t>(size),
      .mode = 0444,
  };
}

// Locates `member` in an already-parsed manifest, normalizing both sides so the stored spelling and
// the caller's spelling never have to agree exactly.
absl::StatusOr<std::reference_wrapper<const Entry>> FindEntry(
    const std::vector<Entry>& entries,
    std::string_view member) {
  const std::string_view wanted = NormalizeMemberName(member);
  for (const Entry& entry : entries) {
    if (NormalizeMemberName(entry.member.path) != wanted) {
      continue;
    }
    if (entry.member.is_directory) {
      return absl::FailedPreconditionError(absl::StrCat("phar member is a directory and has no content: ", member));
    }
    const std::uint32_t how = entry.flags & kFlagCompressionMask;
    if (how != 0 && how != kFlagCompressedGz && how != kFlagCompressedBz2) {
      // A method PHP does not currently write. Saying so beats handing back compressed bytes that
      // would read as binary garbage.
      return absl::UnimplementedError(
          absl::StrCat("phar member uses an unknown compression method ", how, ": ", member));
    }
    return std::cref(entry);
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
  // XFF_HOST_IO: PHAR adapter reads its explicitly selected host container.
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
  MBO_ASSIGN_OR_RETURN(std::string prefix, ReadFileRange(path, 0, kStubScanLimit));
  const std::optional<std::size_t> manifest_at = ManifestOffset(prefix);
  if (!manifest_at.has_value()) {
    return absl::InvalidArgumentError(
        absl::StrCat("not a phar: no __HALT_COMPILER(); token in the first ", kStubScanLimit, " bytes of ", path));
  }
  Cursor cursor(std::string_view(prefix).substr(*manifest_at));
  MBO_ASSIGN_OR_RETURN(const std::uint32_t manifest_length, cursor.Uint32("manifest length"));
  if (manifest_length > kMaxManifestBytes) {
    return absl::DataLossError(
        absl::StrCat("phar manifest declares ", manifest_length, " bytes, over the ", kMaxManifestBytes, " cap"));
  }
  const std::size_t needed = *manifest_at + 4 + manifest_length;
  if (needed <= prefix.size()) {
    return prefix;
  }
  return ReadFileRange(path, 0, needed);
}

// A phar compresses a MEMBER, not the container: the manifest says which method and the bytes sit at an
// offset as a bare stream, so neither libarchive format nor filter applies and the reader inflates it
// itself. The manifest's uncompressed size is the output length - it is untrusted, so both paths cap
// the output at exactly that many bytes and report a mismatch rather than growing without bound.
// Declared here, defined after the two decompressors: what the manifest's flags say the member's
// STORED bytes are, turned into its content. Uncompressed is the common case and copies nothing extra.
absl::StatusOr<std::string> Decompress(const Entry& entry, std::string_view stored, std::string_view member);

absl::StatusOr<std::string> Inflate(std::string_view compressed, std::uint64_t expected_size) {
  // windowBits = -15: a RAW deflate stream, no zlib or gzip wrapper, which is what PHP writes.
  ::z_stream stream{};
  if (::inflateInit2(&stream, -15) != Z_OK) {
    return absl::ResourceExhaustedError("cannot initialize the deflate decompressor");
  }
  // The output buffer is OURS, so it is declared in zlib's own element type and converted to a
  // std::string once at the end - no cast at all. The input is a view of someone else's chars, so it
  // still needs one (plus a const_cast: next_in is non-const even though inflate only reads it).
  std::vector<::Bytef> out(expected_size);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-const-cast)
  stream.next_in = reinterpret_cast<::Bytef*>(const_cast<char*>(compressed.data()));
  stream.avail_in = static_cast<::uInt>(compressed.size());
  stream.next_out = out.data();
  stream.avail_out = static_cast<::uInt>(out.size());
  const int status = ::inflate(&stream, Z_FINISH);
  const std::size_t produced = out.size() - stream.avail_out;
  ::inflateEnd(&stream);
  if (status != Z_STREAM_END || produced != expected_size) {
    return absl::DataLossError(
        absl::StrCat(
            "deflate member did not decompress to its declared ", expected_size, " bytes (got ", produced, ")"));
  }
  return std::string(out.begin(), out.end());
}

absl::StatusOr<std::string> Bunzip2(std::string_view compressed, std::uint64_t expected_size) {
  std::string out(expected_size, '\0');
  auto produced = static_cast<unsigned int>(out.size());
  // Same story as zlib: the source is read-only to bzip2 but its signature does not say so.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  char* const source = const_cast<char*>(compressed.data());
  const int status =
      ::BZ2_bzBuffToBuffDecompress(out.data(), &produced, source, static_cast<unsigned int>(compressed.size()), 0, 0);
  if (status != BZ_OK || produced != expected_size) {
    return absl::DataLossError(
        absl::StrCat("bzip2 member did not decompress to its declared ", expected_size, " bytes (got ", produced, ")"));
  }
  return out;
}

absl::StatusOr<std::string> Decompress(const Entry& entry, std::string_view stored, std::string_view member) {
  const std::uint32_t how = entry.flags & kFlagCompressionMask;
  if (how == 0) {
    return std::string(stored);
  }
  const auto size = static_cast<std::uint64_t>(entry.member.size < 0 ? 0 : entry.member.size);
  if (how == kFlagCompressedGz) {
    return Inflate(stored, size);
  }
  if (how == kFlagCompressedBz2) {
    return Bunzip2(stored, size);
  }
  return absl::UnimplementedError(absl::StrCat("phar member uses an unknown compression method ", how, ": ", member));
}

}  // namespace

absl::StatusOr<PharLayout> ParsePharLayout(std::string_view bytes) {
  MBO_ASSIGN_OR_RETURN(const WholePhar whole, ParseWholePharLayout(bytes));
  PharLayout layout{
      .manifest_length_at = whole.manifest_length_at,
      .manifest_start = whole.manifest_start,
      .manifest_size = whole.manifest_size,
      .entries_offset = whole.entries_offset,
      .data_offset = whole.manifest_start + whole.manifest_size,
      .data_end = whole.manifest_start + whole.manifest_size,
      .global_flags = whole.global_flags,
  };
  layout.members.reserve(whole.entries.size());
  for (const Entry& entry : whole.entries) {
    layout.members.push_back(
        PharMemberLayout{
            .name = entry.stored_name,
            .entry_offset = whole.manifest_start + entry.entry_offset,
            .entry_size = entry.entry_size,
            .data_offset = entry.data_offset,
            .stored_size = entry.stored_size,
        });
    layout.data_end = entry.data_offset + entry.stored_size;
  }
  if (layout.data_end > bytes.size()) {
    return absl::DataLossError(
        absl::StrCat(
            "phar data section is truncated: members end at ", layout.data_end, ", the file has ", bytes.size(),
            " bytes"));
  }
  return layout;
}

absl::StatusOr<std::vector<Member>> ListPharMembers(std::string_view bytes) {
  MBO_ASSIGN_OR_RETURN(WholePhar whole, ParseWholePharLayout(bytes));
  const bool stored_stub = HasStoredStub(whole.entries);
  std::vector<Member> members = MembersOf(std::move(whole.entries));
  if (!stored_stub) {
    members.push_back(StubMember(whole.manifest_length_at));
  }
  return members;
}

absl::StatusOr<std::vector<Member>> ListPharMembersOfFile(std::string_view path) {
  MBO_ASSIGN_OR_RETURN(const std::string header, ReadHeader(path));
  return ListPharMembers(header);
}

absl::StatusOr<std::string> ReadPharMember(std::string_view bytes, std::string_view member, std::uint64_t max_bytes) {
  MBO_ASSIGN_OR_RETURN(const WholePhar whole, ParseWholePharLayout(bytes));
  if (!HasStoredStub(whole.entries) && NormalizeMemberName(member) == kPharStubMember) {
    MBO_RETURN_IF_ERROR(CheckLimit(whole.manifest_length_at, max_bytes, member));
    return std::string(bytes.substr(0, whole.manifest_length_at));
  }
  const std::vector<Entry>& entries = whole.entries;
  MBO_ASSIGN_OR_RETURN(const std::reference_wrapper<const Entry> entry_ref, FindEntry(entries, member));
  const Entry& entry = entry_ref.get();
  MBO_RETURN_IF_ERROR(CheckLimit(static_cast<std::uint64_t>(entry.member.size), max_bytes, member));
  MBO_RETURN_IF_ERROR(CheckLimit(entry.stored_size, max_bytes, member));
  if (entry.data_offset + entry.stored_size > bytes.size()) {
    return absl::DataLossError(absl::StrCat("phar member data runs past the end of the container: ", member));
  }
  const std::string_view stored = bytes.substr(entry.data_offset, entry.stored_size);
  return Decompress(entry, stored, member);
}

absl::StatusOr<std::string> ReadPharMemberOfFile(
    std::string_view path,
    std::string_view member,
    std::uint64_t max_bytes) {
  MBO_ASSIGN_OR_RETURN(const std::string header, ReadHeader(path));
  MBO_ASSIGN_OR_RETURN(const WholePhar whole, ParseWholePharLayout(header));
  if (!HasStoredStub(whole.entries) && NormalizeMemberName(member) == kPharStubMember) {
    MBO_RETURN_IF_ERROR(CheckLimit(whole.manifest_length_at, max_bytes, member));
    return ReadFileRange(path, 0, whole.manifest_length_at);
  }
  const std::vector<Entry>& entries = whole.entries;
  MBO_ASSIGN_OR_RETURN(const std::reference_wrapper<const Entry> entry_ref, FindEntry(entries, member));
  const Entry& entry = entry_ref.get();
  MBO_RETURN_IF_ERROR(CheckLimit(static_cast<std::uint64_t>(entry.member.size), max_bytes, member));
  MBO_RETURN_IF_ERROR(CheckLimit(entry.stored_size, max_bytes, member));
  MBO_ASSIGN_OR_RETURN(
      const std::string content, ReadFileRange(path, entry.data_offset, static_cast<std::size_t>(entry.stored_size)));
  if (content.size() < entry.stored_size) {
    return absl::DataLossError(absl::StrCat("phar member data runs past the end of the container: ", member));
  }
  return Decompress(entry, content, member);
}

}  // namespace xff::archive
