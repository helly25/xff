#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Regenerates the committed PACKAGE-format fixtures.

    tools/make_archive_fixtures.py

Written with the Python standard library only, deliberately: these formats are zip / tar / ar / cpio
wrappers, so no packaging toolchain (jar, dpkg-deb, rpmbuild, npm) has to exist on the machine or in
CI. The phar fixtures are different - they need PHP, which is why they have their own generator.

Why these files exist at all: almost every "package format" is a zip or a tar underneath, so
libarchive already reads them and xff already dives into them. That is behaviour we ship and did not
test. Each fixture below pins one wrapper shape:

    example.jar        zip (also stands for war/ear, apk/aab, vsix, xpi, docx, nupkg)
    example.whl        zip with Python packaging layout
    npm-example.tgz    gzip-filtered tar, the npm / Cargo `.crate` / OCI-layer shape
    example.gem        tar whose members are THEMSELVES archives (nested, one layer visible)
    example.deb        ar with debian-binary + control.tar.gz + data.tar.gz
    example.rpm        rpm lead + header, then a gzip-filtered cpio payload (the `rpm` FILTER path)
    sfx-example.zip    a zip behind a text prefix, offsets absolute - the self-extracting shape
    example.crx        Cr24 header + a zip appended verbatim, so its offsets are RELATIVE
    example.jmod       JMOD's 4-byte magic + a zip appended verbatim

The last three are the "prefixed payload" question: whether a reader finds a payload that does not
start at byte 0, and whether it copes both with absolute offsets (a real SFX) and with the naive
concatenation that CRX and JMOD actually use. The test records the answer rather than assuming it.

Output is deterministic (fixed mtimes, no gzip timestamps), so regenerating without a content change
produces no diff.
"""

from __future__ import annotations

import gzip
import io
import pathlib
import struct
import tarfile
import zipfile

OUT = pathlib.Path(__file__).resolve().parent.parent / "extra_modules" / "archive" / "test_data"

# A fixed timestamp, so the bytes are reproducible.
MTIME = 1_700_000_000
ZIP_DATE = (2023, 11, 14, 22, 13, 20)

# The file every fixture carries, so one assertion works across formats. The needle line is there for
# the content predicates.
COMMON = "data/readme.txt"
COMMON_CONTENT = b"This is the xff archive fixture.\nfindable-needle\n"


def zip_bytes(members: dict[str, bytes]) -> bytes:
    """A deterministic zip holding `members`."""
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, content in members.items():
            info = zipfile.ZipInfo(name, date_time=ZIP_DATE)
            info.external_attr = 0o644 << 16
            archive.writestr(info, content)
    return buffer.getvalue()


def tar_bytes(members: dict[str, bytes], compress: bool = False) -> bytes:
    """A deterministic tar (optionally gzipped) holding `members`."""
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.PAX_FORMAT) as archive:
        for name, content in members.items():
            info = tarfile.TarInfo(name)
            info.size = len(content)
            info.mtime = MTIME
            info.mode = 0o644
            info.uid = info.gid = 0
            info.uname = info.gname = "root"
            archive.addfile(info, io.BytesIO(content))
    # tarfile pads out to its 10 KiB record size, which for these fixtures is almost all zeros. A tar
    # only requires the two zero blocks that mark the end, so trim the rest and stay inside the
    # fixture size budget.
    data = raw.getvalue().rstrip(b"\0")
    data += b"\0" * (-len(data) % 512)
    data += b"\0" * 1024
    return gzip_bytes(data) if compress else data


def gzip_bytes(data: bytes) -> bytes:
    """`data` gzipped with no timestamp, so the result is reproducible."""
    buffer = io.BytesIO()
    with gzip.GzipFile(fileobj=buffer, mode="wb", mtime=0) as compressor:
        compressor.write(data)
    return buffer.getvalue()


def ar_bytes(members: dict[str, bytes]) -> bytes:
    """A System V / GNU `ar` archive - what a .deb is. Hand-written: Python has no ar writer.

    Each member carries a 60-byte ASCII header and is padded to an even length.
    """
    out = [b"!<arch>\n"]
    for name, content in members.items():
        header = (
            f"{name:<16}{MTIME:<12}{0:<6}{0:<6}{0o100644:<8o}{len(content):<10}".encode()
            + b"`\n"
        )
        assert len(header) == 60, len(header)
        out.append(header)
        out.append(content)
        if len(content) % 2:
            out.append(b"\n")
    return b"".join(out)


def cpio_newc_bytes(members: dict[str, bytes]) -> bytes:
    """A cpio archive in the `newc` (SVR4) format - an rpm's payload.

    Every field is 8 ASCII hex digits; the name and the data are each padded to a 4-byte boundary,
    and the stream ends with the zero-length `TRAILER!!!` entry.
    """

    def entry(name: str, content: bytes, ino: int, mode: int) -> bytes:
        name_bytes = name.encode() + b"\0"
        fields = [
            ino,
            mode,
            0,  # uid
            0,  # gid
            1,  # nlink
            MTIME,
            len(content),
            0,  # devmajor
            0,  # devminor
            0,  # rdevmajor
            0,  # rdevminor
            len(name_bytes),
            0,  # check
        ]
        head = b"070701" + b"".join(f"{value:08X}".encode() for value in fields)
        block = head + name_bytes
        block += b"\0" * (-len(block) % 4)
        block += content
        block += b"\0" * (-len(block) % 4)
        return block

    out = b""
    for index, (name, content) in enumerate(members.items(), start=1):
        out += entry(name, content, index, 0o100644)
    out += entry("TRAILER!!!", b"", 0, 0)
    return out


def rpm_bytes(payload: bytes) -> bytes:
    """An rpm around `payload`: the 96-byte lead, one empty header, then the payload.

    libarchive reads rpms through a FILTER rather than a format: it skips the lead, walks each
    header by its declared index-count and data size, skips the zero padding, and hands whatever
    follows to the format bidders - a gzip-filtered cpio here. So a minimal-but-valid lead and header
    are enough to exercise exactly the path a real rpm takes, without an rpmbuild dependency.
    """
    lead = struct.pack(
        ">4sBBhh66shh16s",
        b"\xed\xab\xee\xdb",  # magic
        3,  # major
        0,  # minor
        0,  # type: binary package
        1,  # architecture
        b"xff-fixture-1.0-1",  # name, NUL padded by the format
        1,  # os: linux
        5,  # signature type: header-style
        b"",  # reserved
    )
    assert len(lead) == 96, len(lead)
    # Header: magic, version, 4 reserved bytes, index count, data size. Zero of each: nothing to
    # declare, which is all the filter needs to step over it.
    header = b"\x8e\xad\xe8\x01" + b"\0" * 4 + struct.pack(">II", 0, 0)
    return lead + header + payload


def write(name: str, data: bytes) -> None:
    (OUT / name).write_bytes(data)
    print(f"{name:<20} {len(data):6d} bytes")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)

    # Plain zip wrappers. One fixture stands for the whole family (war/ear, apk/aab, vsix, xpi,
    # docx/odt, nupkg): identical container, different file extension and manifest names.
    write(
        "example.jar",
        zip_bytes(
            {
                "META-INF/MANIFEST.MF": b"Manifest-Version: 1.0\nMain-Class: com.example.App\n",
                "com/example/App.class": b"\xca\xfe\xba\xbe\x00\x00\x00A" + b"stub",
                COMMON: COMMON_CONTENT,
            }
        ),
    )
    write(
        "example.whl",
        zip_bytes(
            {
                "pkg/__init__.py": b"VERSION = '1.0'\n",
                "pkg-1.0.dist-info/METADATA": b"Metadata-Version: 2.1\nName: pkg\nVersion: 1.0\n",
                "pkg-1.0.dist-info/RECORD": b"pkg/__init__.py,,\n",
                COMMON: COMMON_CONTENT,
            }
        ),
    )

    # A gzip-filtered tar: npm packs this way (and so do Cargo `.crate` files and OCI layers),
    # everything under a single top-level directory.
    write(
        "npm-example.tgz",
        tar_bytes(
            {
                "package/package.json": b'{"name":"xff-fixture","version":"1.0.0"}\n',
                "package/index.js": b"module.exports = 'xff fixture';\n",
                f"package/{COMMON}": COMMON_CONTENT,
            },
            compress=True,
        ),
    )

    # A .gem is a tar whose members are themselves archives, so ONE layer is visible without
    # recursion: the test pins that we see `data.tar.gz` as a member rather than its contents.
    write(
        "example.gem",
        tar_bytes(
            {
                "metadata.gz": gzip_bytes(b"--- !ruby/object:Gem::Specification\nname: xff-fixture\n"),
                "data.tar.gz": tar_bytes({f"lib/xff.rb": b"module Xff; end\n", COMMON: COMMON_CONTENT}, compress=True),
            }
        ),
    )

    # A .deb is an `ar` with three members in a fixed order; the payload is the third.
    write(
        "example.deb",
        ar_bytes(
            {
                "debian-binary": b"2.0\n",
                "control.tar.gz": tar_bytes(
                    {"./control": b"Package: xff-fixture\nVersion: 1.0\nArchitecture: all\n"},
                    compress=True,
                ),
                "data.tar.gz": tar_bytes({f"./usr/share/doc/{COMMON}": COMMON_CONTENT}, compress=True),
            }
        ),
    )

    # An rpm: the filter path, ending in a gzip-filtered cpio.
    write(
        "example.rpm",
        rpm_bytes(
            gzip_bytes(
                cpio_newc_bytes(
                    {
                        "./usr/bin/xff-fixture": b"#!/bin/sh\necho xff fixture\n",
                        f"./usr/share/doc/{COMMON}": COMMON_CONTENT,
                    }
                )
            )
        ),
    )

    # Prefixed payloads. The SFX shape writes the prefix FIRST and then the zip in place, so the
    # recorded offsets are absolute and correct - what a real self-extracting archive looks like.
    sfx_prefix = b"#!/bin/sh\n# a self-extracting stub; the zip starts after this text\nexit 0\n"
    sfx = io.BytesIO()
    sfx.write(sfx_prefix)
    with zipfile.ZipFile(sfx, "a", zipfile.ZIP_DEFLATED) as archive:
        info = zipfile.ZipInfo(COMMON, date_time=ZIP_DATE)
        info.external_attr = 0o644 << 16
        archive.writestr(info, COMMON_CONTENT)
    write("sfx-example.zip", sfx.getvalue())

    # CRX3 and JMOD instead APPEND a plain zip verbatim, so every recorded offset is short by the
    # header length and a reader has to work out the delta itself.
    payload = zip_bytes({"manifest.json": b'{"manifest_version":3,"name":"xff fixture"}\n', COMMON: COMMON_CONTENT})
    crx_header = b"fixture-signature-placeholder"
    write("example.crx", b"Cr24" + struct.pack("<II", 3, len(crx_header)) + crx_header + payload)
    write("example.jmod", b"JM\x01\x00" + zip_bytes({f"classes/{COMMON}": COMMON_CONTENT}))


if __name__ == "__main__":
    main()
