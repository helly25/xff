<?php
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

// Regenerates the committed phar test fixtures with the REFERENCE implementation - PHP itself.
//
//   php -d phar.readonly=0 tools/make_phar_fixtures.php
//
// Why generated once and committed, rather than built during the test: a fixture written by our own
// code proves only that the reader agrees with itself, while a PHP-written one catches a
// misunderstanding of the format. But the tests must not depend on a PHP interpreter being installed
// (nor CI on installing one), and the files are tiny, so they are checked in. Run this only when a
// fixture needs to change, and say why in the commit.
//
// Each variant exists because it is a DIFFERENT thing for a reader to get right:
//
//   plain.phar          native format, stored (uncompressed) members - the base case
//   entrygz.phar        native, per-member DEFLATE (Phar::GZ): same manifest, compressed member data
//   entrybz2.phar       native, per-member bzip2 - a second compression id in the same flag field
//   wholegz.phar.gz     a native phar with the WHOLE FILE gzip-wrapped: the halt token is not even
//                       visible until the container is decompressed
//   wholebz2.phar.bz2   the same for bzip2
//   sha256.phar         native, SHA-256 signed: signature bytes plus `GBMB` trail after the member
//                       data, which must not be mistaken for a member
//   tarbased.phar.tar   the TAR-based phar variant: an ordinary tar, so libarchive reads it and the
//                       phar-specific stub / signature are just members
//   tarbased.phar.tar.gz  tar-based and whole-file gzipped
//   zipbased.phar.zip   the ZIP-based variant
//
// The member set is identical everywhere (same names, same contents, one explicit empty directory),
// so a test can assert the same expectations across variants and only the packaging differs.

declare(strict_types=1);

if (ini_get('phar.readonly')) {
    fwrite(STDERR, "phar.readonly is on; rerun as: php -d phar.readonly=0 " . __FILE__ . "\n");
    exit(1);
}

$out = dirname(__DIR__) . '/extra_modules/archive/test_data';
if (!is_dir($out) && !mkdir($out, 0755, true)) {
    fwrite(STDERR, "cannot create {$out}\n");
    exit(1);
}

// Start from an empty directory (README.md aside). Phar refuses to overwrite an existing conversion
// target, and a stale fixture from a renamed variant would otherwise linger, so regeneration has to be
// idempotent rather than incremental.
foreach (glob("{$out}/*") as $stale) {
    if (basename($stale) !== 'README.md') {
        unlink($stale);
    }
}

// Small, deterministic, and recognisable in a failure message. `data/readme.txt` deliberately holds
// a line the content predicates can search for, and `lib/util.php` is big enough that compressing it
// actually shrinks it (so a compressed fixture differs from a stored one in stored size, not just in
// a flag bit).
const MEMBERS = [
    'bin/run.php' => "<?php\nrequire __DIR__ . '/../lib/util.php';\nxff_fixture_main();\n",
    'lib/util.php' => "<?php\nfunction xff_fixture_main(): void {\n    // padding padding padding padding padding padding padding\n    // padding padding padding padding padding padding padding\n    echo \"xff phar fixture\\n\";\n}\n",
    'data/readme.txt' => "This is the xff phar fixture.\nfindable-needle\n",
];

const EMPTY_DIR = 'var/empty';

function fill(Phar|PharData $phar): void
{
    foreach (MEMBERS as $name => $content) {
        $phar[$name] = $content;
    }
    $phar->addEmptyDir(EMPTY_DIR);
    // Container metadata, which sits between the manifest header and the first member entry: a reader
    // that ignores its length reads garbage for every member that follows.
    $phar->setMetadata(['generator' => 'xff/tools/make_phar_fixtures.php']);
}

function remove(string $path): void
{
    if (file_exists($path)) {
        unlink($path);
    }
}

// A native (data-format) phar: PHP stub, binary manifest, member data.
function native(string $path, ?int $entryCompression = null, ?int $signature = null): void
{
    remove($path);
    $phar = new Phar($path);
    $phar->startBuffering();
    fill($phar);
    if ($signature !== null) {
        $phar->setSignatureAlgorithm($signature);
    }
    $phar->stopBuffering();
    if ($entryCompression !== null) {
        // Per-MEMBER compression: the manifest stays plain, each member's data does not.
        $phar->compressFiles($entryCompression);
    }
    unset($phar);
}

// A native phar with the whole file compressed afterwards (`a.phar` -> `a.phar.gz`).
//
// Each caller must pass a DISTINCT base name: PHP keeps open phars in a process-wide registry keyed by
// file name, so re-creating a name whose file was compressed and unlinked fails with "unable to seek
// to start of file".
function nativeWholeFileCompressed(string $path, int $compression): void
{
    remove($path);
    $phar = new Phar($path);
    $phar->startBuffering();
    fill($phar);
    $phar->stopBuffering();
    // No explicit suffix: PHP appends the conventional one (`whole.phar` -> `whole.phar.gz`). The
    // suffix argument REPLACES the extension and must still contain `.phar`, so spelling a bare `gz`
    // is rejected outright.
    $phar->compress($compression);
    unset($phar);
    // Keep only the compressed artifact; the uncompressed twin is `plain.phar` already.
    remove($path);
}

// A tar- or zip-based phar: an ordinary archive that carries the phar's stub and signature as members
// (`.phar/stub.php`, `.phar/signature.bin`), so a generic archive reader handles it with no phar
// knowledge at all.
//
// Built by converting a native phar, which is how PHP itself makes these. `PharData` is NOT usable
// here: it is for non-executable archives and rejects a name containing `.phar`, while these fixtures
// must carry the real, `.phar`-named, executable spelling. `$base` is the native phar that gets
// converted; convertToExecutable() writes the archive-based twin beside it and returns it, so the base
// is deleted afterwards (and must be a name of its own, per the registry caveat above).
function archiveBased(string $base, int $format, ?int $wholeFileCompression = null): void
{
    remove($base);
    $phar = new Phar($base);
    $phar->startBuffering();
    fill($phar);
    $phar->stopBuffering();
    $converted = $phar->convertToExecutable($format, $wholeFileCompression ?? Phar::NONE);
    unset($converted, $phar);
    remove($base);
}

native("{$out}/plain.phar");
native("{$out}/entrygz.phar", Phar::GZ);
if (extension_loaded('bz2')) {
    native("{$out}/entrybz2.phar", Phar::BZ2);
} else {
    fwrite(STDERR, "skipped entrybz2.phar / whole.phar.bz2: the bz2 extension is not loaded\n");
}
native("{$out}/sha256.phar", null, Phar::SHA256);
nativeWholeFileCompressed("{$out}/wholegz.phar", Phar::GZ);
if (extension_loaded('bz2')) {
    nativeWholeFileCompressed("{$out}/wholebz2.phar", Phar::BZ2);
}
archiveBased("{$out}/tarbased.phar", Phar::TAR);
archiveBased("{$out}/targz.phar", Phar::TAR, Phar::GZ);
archiveBased("{$out}/zipbased.phar", Phar::ZIP);

foreach (glob("{$out}/*") as $file) {
    if (basename($file) === 'README.md') {
        continue;
    }
    printf("%-24s %6d bytes\n", basename($file), filesize($file));
    if (filesize($file) > 10 * 1024) {
        fwrite(STDERR, "WARNING: " . basename($file) . " exceeds the 10k fixture budget\n");
    }
}
