<!-- SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com) -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# 0.3.0

## Archives

- `--archive` / `-z` dives for real in a build with the archive extra
  (`--//xff:xff_archive`): a container is visited as the file it is and then descends like
  a directory, so its members are ordinary entries at `container!member` paths that
  `-name`, `-type`, `-size` and friends match unchanged. `--archive=roots` (the xff-family
  default) dives only an archive named as a search root; `--archive=all` (`-z+`, or bare
  `--archive`) also dives archives met mid-walk, at the position a directory of that name
  would take under every `--sort`. `-prune`, `-quit` and `-maxdepth` apply to members as
  they do everywhere else. Without the extra, asking for diving remains a hard error.
- `--archive-separator` / `--archive-prefix` now reach the walk, so printed member paths
  round-trip through the flags that produced them.
- Everything that reads an entry works on members: `-content`, `-icontent`, `-rxc`,
  `-grep`, `-hash`, `-hasheq`, `{hash}` and `{lines}` read a member's bytes out of its
  container, so a member's digest equals the digest of the same bytes on disk.
- `--archive-depth=N` bounds how many CONTAINERS deep diving goes (default 1, so an
  archive inside an archive stays a plain member). A nested container has no path of its
  own, so its bytes are read out of its parent and mounted from memory; `-grep` and the
  rest then work at any depth. The cap is its own knob rather than part of `-maxdepth`,
  because nesting is where a decompression bomb lives.
- A plain text file is no longer mistaken for an archive. libarchive's "every format"
  set includes `mtree`, a magic-less text format, so `xff notes.txt` could report a
  bogus member; the reader now enables its formats explicitly and leaves `mtree` out.

## Internal

- MemorySanitizer is a hard CI gate on Linux. The instrumented C++ standard library it
  needs now comes from the toolchain itself (`toolchains_llvm`'s `--features=msan` plus a
  prebuilt instrumented-libc++ overlay) instead of a cmake/ninja build of the LLVM
  runtimes in CI, so the cell is a plain `bazel test` and needs no suppression file.
- The hermetic LLVM toolchain moved to 22.1.8, matching that instrumented libc++.

# 0.2.0

Sharded-file awareness, hash verification, and a help system that is generated end to
end from one source of truth. The complete, always-current reference is
[XFF.md](XFF.md).

## Sharded files

A sharded set (`data-000-of-003`, `data-001-of-003`, ...) is one logical file, and xff
can now treat it that way instead of listing every shard.

- `--shards[=auto|of|dotnum|underscore,...]` collapses each set to a single entry.
  `auto` (the bare flag) recognizes every built-in scheme: `<stem>-<i>-of-<n>`,
  7-Zip-style `<stem>.<NNN>` volumes, and `<stem>_<NNN>`.
- `--shards-show=first|wildcard|count` picks how a collapsed set is displayed - the
  representative shard's path, the masked name (`arc.???`), or the masked name plus the
  shard count. Incomplete sets are annotated, so a missing shard is visible rather than
  silently ignored.
- `--shards-dedup=first|mtime|error` decides what happens when two files claim the same
  index: keep the lexicographically first, keep the newest, or fail.
- `--shard-pattern=REGEX` registers a custom scheme for layouts the built-ins miss.
- `{shard}` renders the number of shards in the set, and the size / statistics fields
  aggregate across the whole set, so `--summary` and `--histogram` count logical files.

## Hashing and verification

- `-hasheq EXPECTED` is true when the file's digest equals EXPECTED, a `{field}` template
  rendered per entry. `-hasheq {def.SUMS}` checks a manifest value and
  `! -hasheq {def.SUMS}` selects drift or corruption. `-hasheq=ALGO[/ENCODING]` shares the
  `-hash` spec grammar (sha256 / hex by default); hex comparison folds case.
- `--summary=hash` groups matches by digest, so identical files collapse into one bucket
  and the count column reads as a duplicate report.

## Time formats

- `--time-zone-suffix=auto|always|never` controls whether a time preset renders its
  trailing zone offset. Formats whose zone is part of their identity (`zulu`,
  `zulu-dense`, `asn1z`) always keep their `Z`.
- ASN.1 `GeneralizedTime` presets: `asn1` (also spelled `generalizedtime`) is
  `YYYYMMDDHHMMSS` in local time with an optional offset, and `asn1z` is the UTC form
  with a mandatory `Z`.

## Help and documentation

- `--help`, `--help=TOPIC`, `--man` and `--markdown` are now rendered from a single help
  document built out of the flag and expression registries, so they cannot drift from
  each other or from the implementation.
- New topics `--help=environment` (every variable xff reads) and `--help=help` (how the
  help system itself is organized), alongside the existing topic set.
- Plain help is colorized, word-wrapped with correct indentation, and respects `--width`
  as well as the terminal size.
- Valued flags document their values as an aligned table instead of an unreadable inline
  synopsis.
- `--pager[=auto|always|never]` (with `--no-pager`) pages the long documentation surfaces
  and never the file listing; `--man` is formatted through a roff formatter first, so it
  reads like `man xff`.

## Fixes

- `--summary` no longer prints a size column when nothing size-worthy was aggregated: a
  count-only summary now reports just the count.
- A topic's flags are no longer listed twice in the full reference.

## Internal

- Sanitizer and lint coverage grew: clang-tidy runs as a hard CI gate over the whole
  tree, and MemorySanitizer joins AddressSanitizer and ThreadSanitizer on Linux (built
  against an instrumented libc++).
- Environment access is centralized behind one read-once, mutex-guarded cache, and flag
  values share a single option-value parser.

# 0.1.0

First release of xff (eXtended File Find): a `find(1)`-compatible file finder
with modern extensions. This is the basic tool - the core is implemented,
tested, and verified in CI on Linux and macOS.

Everything `find` does works the same way. On top of that, xff adds content and
type matching, structured output, per-run summaries and histograms, file
hashing, diffing, and safe deletes, under the find / xff / xfd / rg flavors. The
complete, always-current list of what is supported is the generated reference in
[XFF.md](XFF.md); the roadmap and open design questions are in [TODO.md](TODO.md).

Notable features not yet built (see [TODO.md](TODO.md)):

- archive diving (`--archive`)
- sharded-file handling
- hash verification (files can be hashed; checking a tree against a manifest is
  not done yet)
