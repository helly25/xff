// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
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

#ifndef XFF_ARCHIVE_PHAR_READER_H_
#define XFF_ARCHIVE_PHAR_READER_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "xff/archive/archive_reader.h"

namespace xff::archive {

// A reader for PHP's NATIVE phar container format, presenting the same `Member` shape as the
// libarchive-backed reader in `archive_reader.h`, so the walk and the VFS backend need no
// format-specific knowledge.
//
// Why a hand-written reader: libarchive does not read the native phar format at all. A phar is a PHP
// script (the "stub") that ends in a `__HALT_COMPILER();` token, followed by a binary manifest and
// then the member data - a layout no generic archive library recognises. The tar-based and zip-based
// phar variants (`.phar.tar`, `.phar.zip`) are ordinary tars / zips and are handled by the
// libarchive reader instead; only the native format needs this code.
//
// The format, as parsed here (all multi-byte integers are unsigned little-endian, per PHP's
// `phar_get_32`):
//
//   [stub: arbitrary PHP source ending in `__HALT_COMPILER();`, optionally followed by ` ?>` + EOL]
//   [4 bytes  manifest length, NOT counting these 4 bytes]
//   [4 bytes  number of member entries]
//   [2 bytes  manifest API version, nibble-packed big-endian: 0x11 0x10 is 1.1.1]
//   [4 bytes  global bitmapped flags]
//   [4 bytes  alias length][alias]
//   [4 bytes  serialized metadata length][metadata]
//   [per member: 4 bytes name length][name]
//                [4 bytes uncompressed size][4 bytes mtime][4 bytes stored (compressed) size]
//                [4 bytes CRC32 of the uncompressed content][4 bytes member flags]
//                [4 bytes serialized metadata length][metadata]
//   [member data, in manifest order, each occupying its own stored size]
//   [optional signature][4 bytes signature type][the 4 bytes `GBMB`]
//
// Two conventions worth stating because they are carried by the data rather than by a flag:
//   - a DIRECTORY member is a name with a trailing `/` (PHP drops the slash and marks the entry a
//     directory), so directory entries need no separate flag bit;
//   - the low 9 bits of the member flags are the POSIX permission bits; `0` means the phar never
//     stored any, not mode 0.
//
// A native phar's executable PHP stub is also a file as far as the format is concerned, despite not
// having a manifest entry. The list/read APIs expose it as the synthetic regular member
// `.phar/stub.php`. If a manifest explicitly stores that same path, the stored member wins: listing
// never returns duplicate paths and reading it returns the stored member's content.

// Where the pieces of a native phar SIT, for a caller that has to write one back (removing a member
// means rebuilding the manifest and the data section). Byte ranges rather than values: every surviving
// member's manifest entry and stored bytes are copied verbatim, so nothing is re-encoded and nothing
// is re-compressed.
//
// All offsets are absolute in the container.
struct PharMemberLayout {
  std::string name;              // as STORED, so a directory keeps its trailing `/`
  std::size_t entry_offset = 0;  // this member's manifest entry
  std::size_t entry_size = 0;
  std::uint64_t data_offset = 0;  // this member's stored (possibly compressed) bytes
  std::uint64_t stored_size = 0;
};

struct PharLayout {
  std::size_t manifest_length_at = 0;  // the 4-byte manifest length, i.e. the end of the stub
  std::size_t manifest_start = 0;      // first manifest byte (the member count)
  std::size_t manifest_size = 0;       // as declared by the length field
  std::size_t entries_offset = 0;      // first member entry: the fixed header + alias + metadata end here
  std::size_t data_offset = 0;         // manifest_start + manifest_size
  std::size_t data_end = 0;            // past the last member's stored bytes: where a signature starts
  std::uint32_t global_flags = 0;      // bit 0x10000 is "signed"
  std::vector<PharMemberLayout> members;
};

// Parses `bytes` into the layout above, with the same error contract as ListPharMembers
// (InvalidArgument = not a phar, DataLoss = a phar whose manifest does not check out).
absl::StatusOr<PharLayout> ParsePharLayout(std::string_view bytes);

// Lists the members of the phar in `bytes` (a whole in-memory phar), including `.phar/stub.php` as
// described above.
//
// Returns InvalidArgumentError when the data is not a native phar (no `__HALT_COMPILER();` token
// within the scanned prefix, or a truncated header), and DataLossError when the token is there but
// the manifest that follows is inconsistent - truncated, or declaring more members than it holds. As
// in the libarchive reader, the split matters: the walk treats "not a phar" as an ordinary file and
// only reports the corrupt case.
absl::StatusOr<std::vector<Member>> ListPharMembers(std::string_view bytes);

// Lists the members of the phar file at `path`. Reads only what the manifest needs (the stub prefix
// plus the declared manifest length), never the member data, so listing a large phar stays cheap.
// Same error contract as ListPharMembers.
absl::StatusOr<std::vector<Member>> ListPharMembersOfFile(std::string_view path);

// Reads ONE member's content out of the phar in `bytes`.
//
// `member` is matched normalized, exactly as `ReadMemberOfFile` does it: a leading `./` and a
// trailing `/` are ignored on either side, so a lookup for `dir` finds the directory member and is
// told it has no content rather than "no such member".
//
// Errors, all distinguishable on purpose: InvalidArgument (not a phar), DataLoss (corrupt manifest,
// or a member whose data runs past the end of the container), NotFound (no such member),
// FailedPrecondition (the member is a directory, so there is no content to read), ResourceExhausted
// when `max_bytes` (0 = unlimited) would be exceeded, and Unimplemented for a per-member COMPRESSED
// entry (phar may deflate or bzip2 individual members; decompressing them is a separate slice, and
// silently returning the compressed bytes would be worse than refusing).
absl::StatusOr<std::string> ReadPharMember(
    std::string_view bytes,
    std::string_view member,
    std::uint64_t max_bytes = 0);

// Reads ONE member's content out of the phar file at `path`, reading only the manifest and then that
// member's own byte range rather than the whole container. Same error contract as ReadPharMember.
absl::StatusOr<std::string> ReadPharMemberOfFile(
    std::string_view path,
    std::string_view member,
    std::uint64_t max_bytes = 0);

}  // namespace xff::archive

#endif  // XFF_ARCHIVE_PHAR_READER_H_
