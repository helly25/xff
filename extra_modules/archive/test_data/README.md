# phar test fixtures

Binary phar containers used by `//:phar_fixture_test`, generated **by PHP itself** with
[`tools/make_phar_fixtures.php`](../../../tools/make_phar_fixtures.php):

```sh
php -d phar.readonly=0 tools/make_phar_fixtures.php
```

They are committed rather than built during the test, on purpose:

- A fixture written by our own writer would only prove the reader agrees with itself. One written by
  the reference implementation catches a misreading of the format - which is the whole risk in a
  hand-written reader for a format libarchive does not support.
- The tests must not need a PHP interpreter, and CI must not install one. The files are tiny (each
  well under 10 KiB), so checking them in costs less than the dependency would.

These are binary containers that BEGIN with PHP source, so a content sniff calls them text: the
`end-of-file-fixer` / `trailing-whitespace` pre-commit hooks appended a newline and silently broke
three of them (every functional test still passed - a byte at EOF moves nothing the reader looks at,
but the signature covers the whole file). Those hooks now exclude archive extensions, and
`phar_fixture_test` asserts the signature's `GBMB` trailer is still the last four bytes of each
uncompressed native fixture, so the same accident fails loudly next time.

Regenerate only when a fixture genuinely has to change, and say why in the commit. Timestamps and
signatures make the bytes differ on every run, so a gratuitous regeneration is pure diff noise.

| File                | Variant                                                                         |
| :------------------ | :------------------------------------------------------------------------------ |
| `plain.phar`        | native format, stored (uncompressed) members - the base case                    |
| `entrygz.phar`      | native, per-member DEFLATE (`Phar::GZ`): plain manifest, compressed member data |
| `entrybz2.phar`     | native, per-member bzip2 - a second compression id in the same flag field       |
| `sha256.phar`       | native, SHA-256 signed: signature plus the `GBMB` trail after the member data   |
| `wholegz.phar.gz`   | native, then the WHOLE FILE gzipped - the halt token is not even visible        |
| `wholebz2.phar.bz2` | the same for bzip2                                                              |
| `tarbased.phar.tar` | the TAR-based phar variant: an ordinary tar, stub and signature are members     |
| `targz.phar.tar.gz` | tar-based and whole-file gzipped                                                |
| `zipbased.phar.zip` | the ZIP-based variant                                                           |

Every variant carries the **same member set** (`bin/run.php`, `lib/util.php`, `data/readme.txt`, and
the explicit empty directory `var/empty`), so one set of expectations covers them all and only the
packaging differs.
