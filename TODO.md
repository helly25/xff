# TODO

Open, cross-cutting decisions to revisit. Code-level TODOs live in comments;
deferred features live in the CHANGELOG. This file is for design choices that are
shipped one way but not yet settled.

## Open decisions

- **FIXED (2026-08-13): an unknown VALUE on a known global was silently ignored.** `--color=bogus`,
  `--sort=bogus`, `--case=bogus` and `--pager=bogus` all exited 0 and behaved as the default. It was
  uniform, so it read as a design choice rather than one flag's oversight - but it was the opposite
  of the choice #102 made for unknown flag NAMES ("unknown option" is a usage error precisely so a
  typo cannot be silently ignored), and the failure mode is worse for values: `--case=insensitve`
  matched case-sensitively and the run looked like it worked.
  - Both obstacles are resolved rather than worked around. A flag now DECLARES how its value is
    checked (`GlobalFlag::ValueCheck` = none / enum / bool / tri-state), so the tri-state flags keep
    the whole shared vocabulary while the enumerated ones check against their own `values` table -
    the same table the help prints, so the error and the documentation cannot disagree. And the check
    runs in ONE place in main, over the parsed globals, rather than inside each resolver: that is
    what lets it cover `--color` / `--width` / `--pager`, which are scanned from raw argv before the
    parse and have nowhere to report from. `kNone` stays the default, so free-text flags (paths,
    formats, regexes, comma lists that validate themselves) are untouched.
  - Precedent for the strict side already existed: `--skip-vcs`, `--diff-format`, `--diff-algorithm`
    and `--timezone` each rejected an unknown value, which is what made the silence elsewhere an
    inconsistency rather than a policy.
  - Writing it turned up three flags that accept MORE than they document (`--color-scheme`'s alias
    spellings, `--format=md`, and the reserved `--regextype=MATCH`, whose own error is better than a
    generic one). `ValueDoc.hidden` marks those: accepted by the check, omitted from the listing, so
    one table stays the source for both.

- **FIXED (2026-08-13): `on` / `off` were documented but not accepted.** `--time-zone-suffix`'s help
  listed `on` / `off` as synonyms of `always` / `never`, and the shared value parser did not accept
  them, so `--time-zone-suffix=off` silently kept the offset. `--gitignore` had the mirror problem: it
  compared the two literal strings `on` / `off` itself instead of using the shared parser, so
  `--gitignore=yes` silently did nothing and `=auto` was not accepted at all. Both now go through
  `values::ParseTristate`, which gained `on` / `off` - the spelling a switch-shaped flag reads best
  in, and the one the help had been promising.

- **DECIDED (user, 2026-08-13): colour comes from ONE resolved palette, `ls`-derived by default, and
  `-ls` uses it too.** Three statements, one design:
  1. if `xff .` colourises, `xff . -ls` must colourise as well - the same run colouring one and not
     the other is the bug;
  2. the colours should be _the ones `ls` uses_, i.e. `$LS_COLORS` (dircolors) is the DEFAULT source;
  3. a user may want xff's own scheme back - and, symmetrically, someone who likes the `ls` palette
     wants the plain listing to use it too. So the palette is a run-wide CHOICE, not a per-action one.
  - **Shape:** `--color-scheme=ls|xff` (spelling to confirm), resolved once per run beside
    `--color=WHEN`, and consulted by every colourised surface (the plain listing, `-ls`, and any
    future tabular output). `ls` means: parse `$LS_COLORS`, fall back to xff's built-in type scheme
    for anything it does not specify (and entirely, when the variable is unset); `xff` means the
    built-in scheme regardless. Default `ls`, per (2) - which is a BEHAVIOUR CHANGE for anyone whose
    `$LS_COLORS` differs from xff's scheme, and is the point.
  - **`-ls` colours the NAME column only,** as `ls -l` does; the metadata stays plain. Safe by
    construction: the path is the last column, so ANSI escapes cannot disturb the computed widths.
  - **Work:** a dircolors parser (the `key=value:` list: the two-letter type keys `di` / `ln` / `ex`
    / `fi` / `so` / `pi` / `bd` / `cd` / `or` / `mi` / `su` / `sg` / `tw` / `ow` / `st` / `ca`, plus
    the `*.ext=` per-extension entries, which xff's scheme has nothing equivalent to), the
    `$LS_COLORS` env var documented (the `check-env-documented` hook enforces it), the flag, and the
    `-ls` wiring. Two slices: the palette (parser + flag + plain listing), then `-ls`.
  - **SHIPPED** as `--color-scheme=auto|ls|merged|xff` (`auto` default; `ls+xff` / `ls-or-xff` /
    `default` name `auto`, `ls-and-xff` names `merged`), with `-ls` colouring the name column.
  - **`$LSCOLORS` (BSD / macOS) is read too, SHIPPED.** A macOS user who themes `ls` has BSD's
    variable and no dircolors setup at all, so reading only `$LS_COLORS` made "the colours ls uses"
    false on that platform. It is 11 fixed-position fg/bg letter pairs (`di` `ln` `so` `pi` `ex` `bd`
    `cd` `su` `sg` `tw` `ow`), letters `a`..`h` for the eight ANSI colours, uppercase = bold
    foreground / bright background, `x` = terminal default. `$LS_COLORS` WINS when both are set,
    being the richer format. Two consequences worth remembering: a value of any other length is
    ignored WHOLE (position is the key, so a short value would shift every later type by one) and
    then counts as no theme rather than an empty one; and BSD has no per-extension entries and no
    `fi` slot, so `merged` is the interesting scheme there - `ls` alone leaves every regular file
    plain.

- **BUG (help text): `-grep` context has only its LONG flags (raised 2026-08-13, FIXED).**
  `--context=SPEC`, `--after-context=N` and `--before-context=N` all work; the single-dash `-A` / `-B`
  / `-C` that #99 reserved were never added, and the flag help cited grep's spellings ("grep -A",
  "grep -C/-A/-B") in a way that read as if xff had them. The help now says which spelling is xff's
  and which is grep's. Whether the shorts should EXIST is the open question below, because `-A` is
  also the proposed archive umbrella.

- **RESOLVED (2026-08-13, user): the archive shorts are TWO families - `-z` reads, `-Z` writes.**
  The sign ladder measures ONE axis (how much to look at) and the CASE carries the capability:

  ```
                     read only    + write (--archive-write)
    none              -z-          (error: -Z- contradicts itself)
    roots (default)   -z           -Z
    all               -z+          -Z+
    any               -z++         -Z++
  ```

  This replaces the earlier `-z++` = "all, and writable", which mixed the axes: adding a `+` must
  never arm a destructive capability (the same principle #73 records for `--feature`), and a slipped
  shift key must change which axis you asked for, not both. It also frees the top read rung for
  `any` = `all` without the name gate (the older spelling `--archive-any` stays as a hidden alias),
  which is the "look everywhere, inside everything" convenience #185 asked for.
  - Arming is not doing: a `-Z` run still needs an action that writes (`-delete`, or `-exec` over an
    extracted copy), and `--safe` / `--dry-run` still apply. That is what makes case-as-capability
    acceptable here; if arming alone could destroy something it would need a whole word.
  - **Later wins per AXIS, and the axes stay independent (user, 2026-08-14).** `-z+ -Z++` widens the
    rung to `any` and arms writing; `-z++ -Z` narrows back to `roots`. A lower-case form never
    disarms, so `-Z++ -z-` means "writing armed, reading OFF" - which reads as pointless today (no
    dive, so no member to touch) but is exactly the shape a CREATE / pack action wants: produce an
    archive without diving into existing ones to harvest their members. `-Z-` is therefore not an
    error (the earlier draft refused it) but the full RESET: reading off and writing disarmed,
    overriding an earlier flag or a config file. Its disarm is only observable once reading is turned
    back on (`-Z -Z- -z`), which is how the test pins it.
  - `-A` / `-B` / `-C` stay free for the grep family, as before. `-z*` stays rejected (a bare `-z*`
    errors in zsh and silently expands in bash), and the ladder stops at `++` in both families.
  - `--archive-depth` is deliberately NOT part of any rung: raising the decompression-bomb cap is a
    different decision from looking in more places.

- **Short primaries `-n` / `-p` for `-name` / `-path` (raised 2026-08-13)?** Note what fd's letters
  actually mean before copying them: fd's `-p` is `--full-path` (a MODE flag that makes its single
  positional pattern match the whole path), and fd has no `-n` at all - so this is not "be like fd",
  it is "add xff shorthands". Argument for: `-name` is by far the most-typed primary. Arguments
  against: `-p` sits one letter from `-print`, `-prune` and `-printf`, and a mistyped `-p` that
  silently means "path" is a nasty failure mode; xff also has no other one-letter PRIMARY, so it
  would be a new class. Leaning: skip `-p`, consider `-n` only if a shorthand family (`-n`, `-t` for
  `-type`?) is designed as a set rather than one letter at a time.

- **fd's `-g` / `--glob`: what is it, and do we need it (raised 2026-08-13)?** fd matches its single
  positional pattern as a REGEX by default; `--glob` switches that one pattern to glob semantics.
  xff has no positional pattern - the choice IS the primary (`-name` / `-path` glob, `-regex` /
  `-rxc` regex, `-regextype` to pick the grammar) - so the flag has nothing to switch. Also `-g` is
  already xff's gitignore toggle, and `--glob` would collide conceptually with `--regextype=GLOB`.
  Leaning: nothing to add; worth one line in `--help=styles` so an fd user finds the mapping.

- **fzf-style scoring for `-fuzzy` (raised 2026-08-13).** fzf ranks with a Smith-Waterman-ish
  alignment score and takes an extended pattern syntax (`^prefix`, `suffix$`, `'exact`, `!negate`,
  space = AND, `|` = OR). Two separate questions:
  - **Where the pattern ends is NOT a problem.** fzf's query is itself ONE argument whose terms are
    space-separated inside it (`fzf --query "^src .cc$ !test"`), so the xff spelling is
    `-fuzzy '^src .cc$ !test'` - exactly the quoting `-name '*.cc'` already needs, with the term
    grammar living inside the token. An UNQUOTED multi-term form would need an `-exec`-style `;`
    terminator, which for a matcher reads worse than quoting and would be the only primary in the
    vocabulary to work that way.
  - **Scoring is the real work**, and it is the same open decision #168 already records: a score
    implies an output ORDER, so it needs `--sort=score` plus probably `--top=N`, and an alignment
    search instead of today's greedy scan. The extended syntax is worth having only once matches are
    ranked, since `^`/`$`/`!` without ranking are just a clumsier `-name` / `!`.

- **A shortcut for "all archive features" (raised 2026-08-13; the WRITE half SHIPPED as
  `--archive-write` / `-z++`, `--archive-any` is still its own flag).** The read side is a fair
  convenience:
  `--archive=all --archive-any` is a mouthful for "look everywhere, inside everything". The spelling
  question is the `-A` / `-z` one above. Two constraints whatever the letter: the WRITE flags are
  armed only by a spelling that says write (see above), and `--archive-depth` stays OUT of any
  "everything on" alias - it is the decompression-bomb cap, and raising it is a separate, deliberate
  decision from "look inside more places".

- **An in-memory filesystem for unpacking (raised 2026-08-13).** What it can and cannot do, because
  the answer splits:
  - **An IN-PROCESS filesystem cannot serve `-exec`** - a child needs a path the kernel resolves - but
    a MOUNTED memory filesystem can, and one is usually already there (user, 2026-08-13):
    - **`/dev/shm` (and `$XDG_RUNTIME_DIR`) are tmpfs on Linux (SHIPPED),** so extracting there is
      memory-backed with a real path and no disk write. A temp-DIRECTORY choice, not new machinery:
      `ChooseExtractDirectory` tries `$XDG_RUNTIME_DIR`, then `/dev/shm`, then the ordinary temporary
      directory, and takes a candidate only when the member fits in a quarter of the space it reports
      free - a tmpfs is RAM shared with the whole machine, so a large member still lands on disk. macOS
      has no default equivalent and falls straight through; an `hdiutil attach ram://` disk would be a
      heavyweight per-run setup and is not attempted.
    - **`memfd_create` + `/proc/self/fd/N`** avoids a filesystem entirely and is seekable, but any
      tool that reopens the path by NAME or keys on the extension breaks, and it is Linux-only. Not a
      default; at most an opt-in for pipelines known to cope.
    - **A FUSE mount of the CONTAINER is the DIRECTION (user, 2026-08-13):** the fuse-archive /
      archivemount shape, where every tool gets a real path into the archive and nothing is extracted
      at all. See the dedicated item below; the shipped temporary file stays the portable fallback, and
      the cheap interim improvement is choosing a tmpfs directory for it where the platform has one.
  - **It already serves nesting.** A container inside a container has no path of its own, so
    `OpenContainerBytes` hands the inner reader the bytes its parent read - an in-memory container in
    all but name. Same for the phar rewrite, which is built in memory and written once.
- **DIRECTION (user, 2026-08-13): mount a container with FUSE (`@xff_fuse`), rather than extracting.**
  A mounted container is the answer the extraction flags approximate: `-exec`, `-execdir` and any
  external tool get a real path INTO the archive, `{}` renders as that path, no copy is made, and the
  "your child edited a copy" surprise disappears (a read-only mount makes an in-place editor fail
  honestly instead of succeeding against a temporary file). It is also the only shape that makes a
  member usable by a tool xff never launched.
  - **Our own FUSE server, not a shell-out to `fuse-archive`.** Delegating looks cheaper but loses
    exactly the formats xff added: fuse-archive reads what libarchive reads, so a native phar, a
    prefixed payload and the compressed-single-file case all fall out - and xff's reader already
    handles them behind `vfs::FileSystem`. A FUSE server over that interface serves every container xff
    can open, by construction, and keeps one notion of member paths.
  - **Shape:** a build-time extra (`--//xff:xff_fuse`, the `@xff_archive` pattern), libfuse on Linux and
    macFUSE on macOS (a kernel extension the USER installs, so the extra must degrade to extraction
    when it is absent rather than fail). Read-only first; a writable mount would be how `-delete` on a
    member and an in-place editor could work later, and it is a separate decision.
  - **DECIDED (2026-08-13, user): explicit flag, in-process server, and the rest as proposed.**
    A mount is process-global state other programs can see, it needs a user-installed kernel
    component on macOS, and not everyone will get it running - so it is an explicit `--archive-mount`
    rather than implicit-when-available, and the same command cannot behave differently on two
    machines by accident. The server runs in-process (a background FUSE thread) with one mount point
    per RUN under `$XDG_RUNTIME_DIR/xff/<pid>/` (else `$TMPDIR`), one subdirectory per container,
    read-only; unmount by RAII at exit plus a signal handler (INT / TERM / HUP) that unmounts and
    re-raises, `fusermount3 -uz` / `umount -f` as the crash path, and a startup sweep of our own
    `xff/<pid>` directories whose pid is gone. Mounts do NOT nest (only the outer container is
    mounted; an inner one is read by xff's own reader, since mounting it would mean materialising its
    bytes), and the WALK does not read through the mount - it keeps using the VFS, so a mount failure
    can change what `-exec` can reach but never what xff finds. Revisit any of it if it proves wrong
    in practice.
  - **What was on the list to decide:** the mount lifecycle (mount per run under a per-pid
    directory, unmounted at exit AND on a signal, with `fusermount -uz` / `umount -f` as the crash
    path, since a stale mount is worse than a stale temp file); whether the mount is implicit when
    available or an explicit flag (a mount is process-global state other programs can see, which argues
    for explicit); how `--archive-depth` maps onto nested mounts; and whether the walk itself should
    read through the mount (simpler: it keeps using the reader, and only child processes see the mount).
  - **Relation to the shipped flags:** `--archive-extract` becomes the portable fallback rather than the
    only mechanism, and `--archive-aggregate` / `--archive-delete` are unaffected.
  - **BUILD PLAN (2026-08-15, epic #183; each bullet one PR).** The one open architectural choice is
    how the extra reaches libfuse, and the ratified "degrade when absent" semantics decide it:
    **dlopen at runtime against vendored API headers** (`fuse_lowlevel.h` interface, permissive
    license), probing `libfuse3.so.3` on Linux and macFUSE's `libfuse.2.dylib` on macOS. Rejected:
    system `linkopts = ["-lfuse3"]` (breaks the hermetic build AND makes absence a startup failure
    instead of a degrade) and vendoring libfuse as a third_party module (its build wants a
    configure-generated config.h per platform, and macFUSE ships its OWN libfuse fork, so a vendored
    Linux build still needs the runtime path on macOS - all cost, no reuse). dlopen is the only shape
    where one binary runs everywhere and mounting is a capability probed per machine. 1. **@xff_fuse skeleton + runtime loader (SHIPPED)**: the extra module (pcre2/archive pattern,
    picked up by `tools/extras.py --wildcards` automatically), `FuseLoader` dlopening the
    platform fuse3 library and eagerly resolving the mount server's symbol set (14 symbols
    since 3b added the direntry builder + readlink) - so "available" MEANS mountable - and
    `FuseAvailable()`. Tests are environment-AGNOSTIC (Linux
    CI images tend to have libfuse3, macOS does not): they pin the invariants of both states.
    fuse2-only installations (older macFUSE) report unavailable by design; revisit when a real
    macFUSE user appears. 2. **Mount lifecycle (SHIPPED, directory half)**: `MountRoot` owns the per-RUN root
    `$XDG_RUNTIME_DIR/xff/<pid>/` (else tempdir) with RAII removal, per-container mount points
    (basename + counter on collision), and `StaleRoots()`/`SweepStaleRoots(unmounter)` - the
    sweep reports and removes dead-pid roots, calling an INJECTED unmounter per mount point so
    the process-spawning `fusermount3 -uz` / `umount -f` crash path and the signal handler land
    with the server (slice 3), which owns actual mounts. All plain-filesystem, tested without
    FUSE. 3. **The read-only FUSE server over `vfs::FileSystem`**, split again on inspection - the fuse3
    ABI surface is the risk, not the callbacks: - **3a. The fuse3 API headers, FETCHED from git into the fuse module (SHIPPED)**. The
    module's MODULE.bazel pins libfuse's **fuse-3.18.2 release asset** (sha256-verified) with
    a BUILD overlay (`libfuse.BUILD.bazel`) exposing the interface-only `fuse3_headers`
    library - the meson-generated `libfuse_config.h` include is patched behind
    `__has_include` and the version macros land as defines; `FUSE_USE_VERSION=30` picks the
    base fuse3 API for the widest runtime match. `FuseApi`
    is the typed call surface: every loader symbol cast ONCE (the funneled dlsym-contract
    NOLINT) into the function types from libfuse's own headers, so `fuse_lowlevel_ops`'s
    layout is never transcribed. No LGPL text in the tree; the NOTICE component lands with
    the slice that links the extra into `xff_full`. - **3b. The server itself (SHIPPED)**: `FuseServer::Mount` serves any `vfs::FileSystem`
    read-only - lookup/getattr/readdir(+release)/open/read/readlink filled by NAME into the
    fetched `fuse_lowlevel_ops`, an only-grows inode table, whole-member content held per
    open handle (decode once, kernel reads in chunks), one loop thread per mount with RAII
    exit-unmount-join-destroy teardown; INT/TERM/HUP ask every live session to exit before
    re-raising. `CrashUnmount` (`fusermount3 -uz` / `umount -f` via posix_spawnp) is the
    unmounter for slice 2's sweep seam. Linux CI mounts a fake filesystem and reads it back
    through the kernel with `XFF_FUSE_REQUIRED=1` so that path can never silently skip
    (test action unsandboxed - /dev/fuse and setuid fusermount3 do not exist in the
    sandbox); macOS exercises the degrade. 4. **CLI**, split: - **4a. Build + identity plumbing (SHIPPED)** (user-flagged 2026-08-16: fuse was absent
    from `--help=extras`): `--//xff:xff_fuse` + `xff_fuse_on` (`xff_all` coverage), xff_full
    links @xff_fuse's registration TU, the `xff_extras_api` fuse slot
    (`MountSupportAvailable`) feeds `ExtraEnabled("fuse")`/`kKnownExtras`/`ExtraBuildFlag`,
    the `--help=extras` row, and the libfuse NOTICE component (LGPL-2.1, interface-only
    headers + runtime dlopen - no LGPL code in the binary; the committed NOTICE stays
    core-only by the license_test contract, extras render live). - **4b-0. Path vocabulary (SHIPPED)**: the server resolved a lookup by joining parent and
    name with `/`, which only a local filesystem understands - the archive VFS spells a member
    `container!member`. Lookup now asks the FILESYSTEM (`ReadDir` reports each child's full
    path in its own vocabulary) instead of assembling one, and the fake in the test uses the
    `!` spelling so a slash-assuming server cannot pass again. The claim itself is pinned
    against REAL bytes by `test_data/mini.tar` - a committed 3.5 KiB uncompressed tar of raw
    512-byte blocks (`hello.txt`, `sub/a.bin`) - in `archive_fs_test`: the reported paths use
    the separator, those exact paths are the ones `Stat`/`ReadContent` answer to, a
    slash-joined path is rejected as InvalidArgument, and directories INSIDE a container keep
    ordinary slashes (so a consumer splits once at the container boundary and never re-joins). - **4b. The flag (SHIPPED)**: `--archive-mount` serves a member from a read-only MOUNT of
    its container instead of a copy. The seam is a mount FACTORY in `xff_extras_api::fuse`
    (registered by @xff_fuse next to the linked-in slot, so a binary cannot advertise mounting
    it lacks); `engine::MountedContainers` mounts once per container, splits the member path
    once at the container boundary, and answers the mounted path. `ExecTargetPath` asks it
    before the extractor, so `{}` is a path INSIDE the archive for `-exec` / `-execdir` and no
    copy is made. Mounting is a per-machine capability: absent extra = the standard hard
    error, absent runtime library or no permission = one line after the walk plus extraction
    (which is what makes the flag safe in a config file); armed without `--archive-extract`
    and unable to mount, the action is refused with a message naming both ways out.
    `IsExtracted` now ASKS the extractor rather than inferring from "differs from the entry's
    path", so a mounted path is never handed to `Release`.
    - **4b root cause (FIXED)**: mounts aborted their connection on the first read on Linux
      (ECONNABORTED, request in flight, daemon alive, no teardown). It was a USE-AFTER-FREE, and
      ThreadSanitizer is what finally named it ("data race in `~ArchiveFileSystem()`"): the walk
      owned the container's reader in a DIVE-SCOPED `unique_ptr` while `MountedContainers` keeps
      mounts for the whole run, so once the walk left the container every FUSE callback served
      freed memory - and a garbage reply is exactly what makes the kernel abort a connection.
      That single fact explains everything the hunt found confusing: it failed on x86_64 CI while
      passing on aarch64, differed between two invocations moments apart, and moved when two
      unrelated `std::cerr` lines were deleted. All allocator timing.
      **The API allowed it**, so the fix is in the type, not the caller: `MountFactory` /
      `MountContainer` / `FuseServer::Mount` take `std::shared_ptr<const vfs::FileSystem>`, and
      the header no longer says "`fs` must outlive the returned Mount" - a promise no compiler
      checks, about an object served from another thread for a whole run. That comment WAS the
      bug, written down. Taking ownership also DELETED bookkeeping: `PathFor` no longer takes a
      filesystem reference plus a separate owner (two parameters that had to agree, unchecked),
      and the struct pairing each mount with its reader is gone. `mount_test`'s
      `AMountKeepsTheReaderAliveAfterTheCallerDropsIt` pins the property through a `weak_ptr`
      with no FUSE involved, so it runs on macOS too.
      Two earlier diagnoses were WRONG and are recorded as such: the libfuse pin (3.18.2 headers
      against a 3.14 runtime) and the mode bits were real improvements that fixed nothing here.
      The pin stays at 3.14.0 on its own merit - libfuse guarantees only BACKWARD compatibility,
      so compile against the OLDEST runtime we must support.
      What hid all of it: the extra's mount tests SKIPPED whenever the loader reported no fuse3,
      ignoring `XFF_FUSE_REQUIRED` - the very guarantee that flag exists for - so the whole kernel
      path reported green without running. An unavailable loader is now fatal where the
      environment promises one, and it prints the loader's reason.
    - **The lesson, made a command**: `tools/fuse_linux_test.sh` (functional / `tsan` / `msan`)
      runs the mounting tests on Linux from a mac before pushing, because macOS skips every one of
      them and `bazel test` therefore goes green without executing a line of the kernel path. Its
      comments carry the traps: TSan needs reduced ASLR entropy on aarch64, the repo's own
      `--config=tsan` pulls an x86_64-only toolchain, and `msan` must force an EMULATED x86_64
      container because the instrumented libc++ overlay ships x86_64-linux only.
    - **The msan cell also needed the sanitizer cells to know what machine they are on**: `tsan`
      and `msan` are standalone jobs, not matrix cells, so they never installed fuse3 or set
      `XFF_FUSE_REQUIRED`, and a runner that CAN mount was failing a test that demanded the
      refusal message. Both declare it now. Every mounting test skips under MSan (the dlopened
      system libfuse3 is uninstrumented, so its bytes read back as uninitialized) - in C++ via
      `MEMORY_SANITIZER`, in the shell test via an `XFF_MSAN` env from a `select()` on the same
      flag. And the 1000-member `archive_fs_test` moved to `size = "medium"`: 1589 ms natively
      times MSan's origin-tracking factor lands right on the small (60s) cliff.
- **4b follow-up (SHIPPED 2026-08-16)**: helly25/bashtest 0.6.0's `skip_test` makes the two
  MSan-guarded CLI mount cases real skips instead of successful tests that merely print a skip
  line; `--no-skip` is available where an environment guarantee must turn any skip into a failure.

- **Bounded member CACHE (SHIPPED).** `-grep`, `{hash}` and `-cmp` on the same member used to
  each decompress it again. `MemberCache` (`member_cache.{h,cc}`) is a mutex-guarded LRU with a
  64 MiB byte cap per open container - the cap is the decompression-bomb concern, so oversized
  content is served but never stored - consulted by `ArchiveFileSystem::ReadContent`. Built as a
  cache keyed by member (the container is the filesystem instance), not as a general in-memory
  VFS with no second customer.

- **Modern (non-`find`) default time format: resolved to `space`.**
  `space` (`2026-06-22 14:30:00 +0100`) is the default: human-first (it matches
  GNU `ls --time-style=long-iso`/`full-iso` and `git log --date=iso`), still ISO-
  ordered so it sorts lexicographically, and parseable back by `ParseTimeString`.
  `--time-format` (config phase D4b) makes this a soft choice rather than a
  lock-in: `rfc3339` (`2026-06-22T14:30:00+01:00`) is one flag
  (`--time-format=rfc3339`) or one `.xffrc` line (`common: --time-format=rfc3339`)
  away for interchange-by-default, and machine consumers use `--format=jsonl`.
  (find's `-printf %t`, once implemented (#48), uses `asctime` per find.)

- **`--timezone` scope and spelling.**
  Shipped (config phase D4a) as `--timezone=ZONE`: overrides the zone used both
  to _interpret_ time-string arguments (`-newerXt`) and to _format_ time fields
  (`{atime}`/`{mtime}`/`{ctime}`/`{btime}`). Accepts `local`/empty,
  `utc`/`z`/`zulu`, and IANA names (`America/New_York`); an unknown zone is a
  usage error. The companion `--time-format=NAME` selector shipped alongside it
  (config phase D4b), and `-printf` (`%a`/`%c`/`%t` + `%Ak`/`%Ck`/`%Tk`) and `-ls`
  both render in the zone (#48). Both follow-ups have now shipped (with the #70
  datetime growth): (a) the `--tz=ZONE` short alias of `--timezone=ZONE`; (b)
  fixed-offset specs (`+05:30`, `-0800`, `+01`), which `ParseTimeZone` builds via
  `absl::FixedTimeZone` since `absl::LoadTimeZone` cannot parse them.

- **Project `.xffrc` layer: resolved - dropped entirely (Option B, 2026-07-06).**
  Decided against any auto-discovered project config (no ancestor cascade, no subtree
  scoping); config is system + user + an explicit `--xffrc=FILE` only. This supersedes the
  earlier subtree-scoping question (now moot). Full record + the `--xffrc` arming restriction
  are in the roadmap tail below ("Config: drop the project `.xffrc` layer").

- **INVESTIGATED (GATES 1.0.0): `--flag:modifier` instead of `--flag=modifier value`. Findings below,
  decision still open.** The surveyed surface is far narrower than the proposal assumes, and one
  example in the original note was simply wrong.

  **Surveyed every global's value grammar.** Only these have any internal structure:

  | flag                           | shape                | is it "modifier + value"?                    |
  | :----------------------------- | :------------------- | :------------------------------------------- |
  | `--define=NAME=VALUE`          | two `=`              | YES - the one genuine case                   |
  | `--histogram=BUCKET[:MEASURE]` | `:` INSIDE the value | no - a sub-selector, and it already owns `:` |
  | `--columns=FIELD,...`          | comma list           | no - a plain valued flag                     |
  | `--diff-ignore=TOKEN,...`      | comma list           | no - a plain valued flag                     |
  | `--timezone=ZONE, --tz=ZONE`   | single value         | no                                           |

  **Correction to the original note:** `-capture=NAME ... \;` and `-capturedir` are NOT globals. They
  are expression PRIMARIES (single dash, `=payload` binding), so they belong to the primary grammar,
  not the `--flag` surface. Respelling those would be a much larger change to the expression language
  and is not what this item proposed.

  **The collision is real and already shipped.** `:` is xff's established SUB-separator INSIDE a value:
  `--histogram=size:count`, the `-printf` bridge `%{size:h}`, and the rewrite qualifier
  `{field:s/pat/repl/}`. Promoting `:` to the flag/modifier separator would put two meanings of `:` in
  one token - `--histogram:size:count` - which is worse than the `=` it replaces.

  **So the proposal buys clarity for exactly one flag** (`--define`), at the cost of a second meaning
  for `:`. Three ways to close it:
  1. **Do nothing.** `--define=NAME=VALUE` stays; it is unambiguous to the parser (split at the FIRST
     `=`) and matches `make`, `cmake -D`, and `bazel --define`. Zero churn, and the precedent argument
     is strong: users have seen `NAME=VALUE` after an `=` before.
  2. **Accept `--define:NAME=VALUE` as an alias**, keeping `=` working. Narrow, no other flag changes,
     no new meaning of `:` inside a value. Costs one alias and its docs.
  3. **Blanket switch** for all valued flags - NOT recommended: it breaks every shipped spelling and
     collides with the sub-separator above.

  **Recommendation: (1), or (2) if the double `=` is what grates** - the decision is yours; nothing is
  implemented. Whichever way, it stops gating 1.0.0 once recorded, because (1) is a no-op and (2) is
  additive (an alias can land after 1.0.0 without breaking anyone).

- **Original proposal (kept for the rationale):**
  Several globals spend their `=value` slot on a _modifier_ (a key, a mode, a dimension) and
  then take the actual value separately, so `=` no longer means "here is the value". The
  proposal is to separate the modifier with a colon - `--flag:modifier` - which frees `=`
  for the value and makes the whole thing expressible in one token:
  `--flag:modifier=value`. Today's spellings that motivate it:
  - `--define=NAME=VALUE` - two `=` in one flag, the clearest case
    (`--define:NAME=VALUE` reads unambiguously);
  - `--capture=NAME CMD... \;` / `--capturedir=NAME ...` - modifier in the flag, value
    (the command) in the following arguments;
  - `--histogram=BUCKET[:MEASURE]` and `--shards[=auto|SCHEME,...]` - a modifier plus a
    sub-selector, already using `:` _inside_ the value.
    The investigation has to settle, at minimum: (a) which flags are genuinely
    modifier-plus-value versus plain valued flags that must keep `=`; (b) the collision with
    `:` as xff's existing _sub_-separator inside values (`BUCKET:MEASURE`,
    `%{field:qualifier}`, `{field:s/pat/repl/}`) - promoting `:` to the flag separator may
    make `--histogram:size:count` unreadable or ambiguous; (c) whether both spellings are
    accepted (`=` as a deprecated alias) or it is a hard switch; (d) the knock-on effects on
    `.xffrc` lines, `--explain`, the generated help / `XFF.md`, and shell completion.
    **Blocking for 1.0.0**: it changes the surface of shipped flags, so after 1.0.0 it would
    be a breaking change rather than a refinement.

## Sanitizer verification: what runs where

**MSan CONFIRMED on x86_64 Linux, 2026-08-16: 124/124 Bazel test targets passed with no
MemorySanitizer findings.** Nine mounting cases were skipped by design (6 in `fuse_server_test`,
1 mount-factory case in `fuse_register_test`, 2 CLI `--archive-mount` cases), for the reason in
"Why every mounting test skips under MSan" below. Everything that does NOT cross into the
uninstrumented system libfuse ran and passed: archive parsing, the FUSE loader and API logic,
mount lifecycle, registration, and the ownership tests.

So the honest statement of coverage is: **no MSan defect exists in our own code, and real FUSE
kernel-mount behaviour is not exercised under MSan at all.** That is not a hole left open - it is
the deliberate boundary of what MSan can say about a dlopened library it did not instrument. The
kernel path has its own unsanitized and TSan coverage (CI, plus `tools/fuse_linux_test.sh`).

### Reproducing it

An **x86_64** Linux machine with `/dev/fuse` (an ordinary VM is fine; `sudo` only for two
packages). aarch64 will NOT do - see "why" below. Then:

```sh
sudo apt-get install -y fuse3 libfuse3-3    # the runtime the loader dlopens + the unmount helper
git clone https://github.com/helly25/xff && cd xff
export XFF_FUSE_REQUIRED=1                  # a SKIPPED mount test is a failure on a machine that can mount

# The MSan cell (this is the command that was run):
bazel test //... $(tools/extras.py --wildcards) --config=xff_docs \
  --config=clang --config=msan --test_env=XFF_FUSE_REQUIRED

# The TSan cell, same shape (already green in CI, cheap to confirm):
bazel test //... $(tools/extras.py --wildcards) --config=xff_docs \
  --config=clang --config=tsan --test_env=XFF_FUSE_REQUIRED
```

Expected: all tests pass, with every MOUNTING test reporting SKIPPED under `--config=msan`
(see below for why that is correct rather than a cop-out).

### Why an x86_64 host specifically

MSan false-positives on anything it did not instrument, so the C++ standard library must be
instrumented too. `--config=msan` gets that from toolchains_llvm's `msan` feature, which swaps in
the instrumented libc++ overlay fetched by `MSAN_LIBCXX_URL` in `bazelmod/llvm.MODULE.bazel` - and
that overlay is published for **x86_64-linux only**. There is no aarch64 build to fetch.

Emulation was tried and does not work. `tools/fuse_linux_test.sh msan` forces a
`--platform linux/amd64` container, which on Apple silicon means qemu; the build gets as far as
linking and then dies with `clang: error: unable to execute command: No such file or directory`

- the toolchain's linker cannot run under the emulation layer. That mode is left in the script
  because it is correct on a real x86_64 host, and its comment says so.

### What IS verified locally, and how

`tools/fuse_linux_test.sh` (from a mac, via colima/docker) exists because **macOS has no fuse3, so
every mounting test skips there and `bazel test` goes green without executing a line of the kernel
path**. Two modes work on aarch64:

- `tools/fuse_linux_test.sh` - the fuse tests plus the CLI mount test, sandboxed as CI runs them.
- `tools/fuse_linux_test.sh tsan` - the CLI mount test under ThreadSanitizer. Needs `--privileged`
  and `sysctl vm.mmap_rnd_bits=28`: aarch64 TSan aborts with "unexpected memory mapping" under the
  default ASLR entropy. It drives the sanitizer through the container's gcc rather than
  `--config=tsan`, because the repo's config pulls the x86_64-only hermetic clang.

Note the container needs `git` installed (MODULE.bazel pulls toolchains_llvm and
hedron_compile_commands through `git_repository`, so a cold cache cannot even compute the repo
mapping) and roughly 25 GB of free VM disk for the hermetic LLVM.

### Why every mounting test skips under MSan (and why that is not a dodge)

A mount runs through the **dlopened system libfuse3**, which MSan did not instrument, so every byte
libfuse writes reads back as uninitialized and the process dies inside `fuse_opt_parse`. There is
nothing to fix in our code: the report is about libfuse's memory, not ours. The skips are therefore
deliberate and keyed off `--config=msan` in one place per language:

- C++: `#if defined(MEMORY_SANITIZER)` (the macro `--config=msan` defines) in `fuse_server_test`
  and in `fuse_register_test`'s factory case.
- Shell: an `XFF_MSAN` env var, set by a `select()` on `//xff:xff_msan_enabled` in
  `xff/cli/BUILD.bazel`, read by `_skip_under_msan` in `xff/cli/full_extras_test.sh`.

To check the guards compile and fire without an MSan toolchain at all:
`bazel test @xff_fuse//... --config=xff_full --copt=-DMEMORY_SANITIZER` (the factory case must
report SKIPPED and the rest must pass), and
`bazel test //xff/cli:full_extras_test --config=xff_full --//xff:xff_msan=true` (both mount cases
must print the skip line, and must NOT print it without the flag).

### Things that bit us, so nobody re-derives them

- **A skipped test looks exactly like a passing one.** `@xff_fuse//:fuse_server_test` reported
  "PASSED in 0.1s" on Linux CI while skipping every mount, because the skip honoured
  `XFF_FUSE_REQUIRED` only on mount FAILURE, not on "no fuse3 here". Hence the env var, and hence
  passing `--test_env=XFF_FUSE_REQUIRED` on every Linux cell.
- **`tsan` and `msan` are standalone jobs, not matrix cells.** They did not install fuse3 or set
  `XFF_FUSE_REQUIRED`, so a runner that CAN mount was failing a test that demanded the refusal
  message. Any new sanitizer job must make the same declaration.
- **MSan makes tests far slower.** `archive_fs_test` reads all 1000 members of `many.tar.gz` in
  1589 ms natively; under MSan's origin tracking that lands past the `small` (60s) budget, and it
  timed out with every assertion still holding. It is `size = "medium"` for that reason - a timeout,
  not a sanitizer finding.
- **Pipes hide exit codes.** `pre-commit run clang-tidy | grep -c warning` reports grep's status and
  discards the gate's; a gate script piped to `tail` reported success while aborting on an unbound
  variable. Capture output, then read the real exit code.

## Remaining work

The backlog of features and infrastructure not yet built. Ordered by current
intent, not hard dependency. Task numbers reference the agent task list.

### Small slices in flight (task ledger)

- **#202 (shipped, PR #541)**: the FORMATS TABLE under `--help=archive` (format, read, write,
  extensions), rendered as a real `Table` node aligned in every backend (plain, markdown, roff);
  the read side registers its formats + suffixes through the seam (`RegisterContainerReadFormats`)
  and the dive gate is DERIVED from that registration, so neither the table nor the gate can drift.
- **#203 (shipped, PR #545)**: `--help=notice` completeness - the extras line comes from
  `EnabledExtras()` (and `ExtraEnabled("pcre2")` is actually wired), zlib/bzip2 register their own
  notice rows as direct phar-reader deps.
- **#204 (shipped, PR #543)**: bad and unknown global flags are hard errors (exit 2) even when
  `--help`/`--man`/`--version` appear: meta flags are noted during the scan, validated with the
  rest, rendered only on a clean parse. Follow-up recorded: meta spellings inside `-exec` argument
  runs are still recognized from raw argv; fold meta handling into the parser proper.

- **#201 (seam + retrieval SHIPPED; extras' own texts deferred)**: extras license TEXTS. `--help=license` renders only
  xff's own Apache-2.0 and `--help=notice` inventories the linked components without their full
  texts - so a binary containing zstd (BSD-3) or, once the fuse extra links, libfuse (LGPL-2.1)
  cannot show what those licenses actually say. Extend the license/notice self-registration seam so
  a component carries (or points at) its full text, and add the retrieval spelling - leaning
  `--help=license=COMPONENT`, with the component names cross-referenced from `--help=notice` and
  an unknown name getting the usual guiding error. #142's rule stands: any license page leads with
  the copyright + grant statement.
  **Shipped**: bodies are registered by SPDX ID rather than by component, because components SHARE
  licenses (libarchive and lz4 are both BSD-2-Clause; Abseil, mbo and xff itself are all
  Apache-2.0) - one registration answers for every component naming it, and duplicate copies cannot
  drift. The seam mirrors the notice one (`LicenseBodyRegistrar`, static-init, function-local static
  registry); re-registering an SPDX keeps the FIRST, so the answer never depends on static-init
  order. xff's generated LICENSE text is the Apache-2.0 registration. The topic name still
  case-folds but its VALUE does not (component names are proper nouns), and the lookup is
  case-insensitive so reading a license is not a spelling test.
  **Deferred**: the extras' own license FILES (BSD-2, BSD-3, Zlib, bzip2, 0BSD, LGPL-2.1). Each
  extra should embed the texts IT brings, through a shared genrule macro modelled on
  `//xff/license:license_text_gen`. Until then those components render an explicit "the full <SPDX>
  text is not embedded in this binary" line under their retained attribution - the license still
  applies, this binary just does not carry its words, and saying so beats implying otherwise.

- **#196 (this change completes it)**: enforce `MBO_ASSERT_OK_AND_ASSIGN` over
  `ASSERT_THAT(x, IsOk())` + deref. The 77-site conversion shipped in PR #528 (+ 4 loop-body
  stragglers here, with `SCOPED_TRACE` carrying the iteration context); this change adds the
  `no-isok-assert-then-deref` pre-commit hook (`tools/check_status_assert.py`) and the explicit
  STYLE_CPP rule.
- **#197 (this change)**: `PackOptionSpec.values` / `.formats` as `absl::Span<const
std::string_view>` over named constexpr arrays instead of comma-joined strings re-split at
  every use.
- **#198 (shipped, PR #525)**: removed the 5 avoidable reinterpret_casts (buffers declared in the
  C API's element type); 3 remain as single funneled NOLINT boundaries.
- **#199 (shipped, PR #524)**: `--help=content` topic gathering the tagged family from both SOTs
  (`Descriptor.topic`) + `regex`/`regexp` aliases for the grammars topic.
- **#200 (shipped, PR #526)**: `--help=topics` index alphabetized at render time; informative
  aliases continue the name in the term column; guessable plurals suppressed.

### Lint / CI / style adoption (from helly25/mbo)

- **clang-tidy does not cover `extra_modules/` (measured 2026-08-15; 81 findings behind the
  exclusion).** The hook skips them (`exclude: ^extra_modules/` in `.pre-commit-config.yaml`) and the
  CI job inherits that, so nothing has ever linted an extras source.
  - **The exclusion's stated reason no longer holds.** It says the extras cannot be parsed because
    their deps are absent from a lean build. With a FRESHLY GENERATED compile DB they parse fine -
    the `'mbo/status/status_macros.h' file not found` seen while investigating was a STALE DB (a dep
    added in #512 without re-running `./compile_commands-update.sh`), not a missing include set.
    Re-generate before concluding anything about a compile-DB symptom.
  - **What the exclusion has been hiding**, by check: 21 `modernize-use-designated-initializers`,
    7 `cppcoreguidelines-pro-type-reinterpret-cast`, 7 `concurrency-mt-unsafe`, 6 unused variables,
    5 pointer arithmetic, 4 `misc-const-correctness`, 4 implicit widening, 3 cognitive complexity,
    3 `hicpp-use-auto`, 3 const_cast, 2 `performance-no-automatic-move`, and a tail of singles.
    Worst files: `archive_writer_test.cc` (18), `pcre2_backend.cc` (15), `phar_reader.cc` (14).
  - **STATUS: DONE (81 -> 0; this change drops the exclusion).** The last three
    cognitive-complexity functions each got their real extraction: `ArchiveFileSystem::Open` ->
    `OpenCompressedSingle` (a private static member, keeping the InvalidArgument-means-keep-probing
    convention), `RemoveMembersOfFile` -> `TransferFirstMember` + `AllRemoved`, and
    `RemovePharMembersOfFile` -> the `SelectSurvivors` / `RebuildPhar` / `AppendSignature` stages.
    Remember the platform caveat: a macOS run cannot lint `#if defined(__linux__)` blocks - the
    Linux CI job is the authority there.
  - **Sequence**: fix per file (the reinterpret-casts in the binary readers and the
    `concurrency-mt-unsafe` hits want judgement, not a blanket rewrite), then DROP the exclusion in
    the last slice - not before, since the CI clang-tidy job is a hard gate and would go red on the
    first one. `archive_pack.cc` / `archive_register.cc` are already clean (#515, #516), as is the
    whole pcre2 extra.
  - **What a C-API binding taught us (pcre2, done):** most of its findings were the boundary itself -
    `PCRE2_SPTR` is `const unsigned char*` where callers hold `const char*`, and the ovector comes
    back as a bare pointer. The answer is not to suppress in bulk: funnel the casts through the one
    function that exists to do them (`Sptr`) and NOLINT it there with the reason, span the ovector so
    offsets are indexed rather than pointer-walked, and fix everything else for real (`std::array`
    for the error buffer, deleted copy/move on the handle-owning class). Expect the same split in
    `phar_reader.cc` and `archive_writer.cc`.

- **Style docs + `.clang-tidy`** (this change). `.clang-tidy` (mbo's rule set),
  `STYLE_CPP.md`, `RULES.md`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, and an
  `AGENTS.md` pointer. The `bazel-compile-commands-extractor` dev module is
  already wired (`bazelmod/dev.MODULE.bazel`), so clang-tidy can run locally.
- **Apply clang-tidy.** `bazel run @bazel_compile_commands_extractor//:refresh_all`
  to produce `compile_commands.json`, run `clang-tidy` across `xff/`, and fix the
  findings (likely several PRs sized by finding count).
- **Adopt trunk.** `.trunk/trunk.yaml` (+ `configs/`) and a CI `trunk` job
  mirroring mbo: buildifier, markdownlint, prettier, yamllint,
  trivy/trufflehog, git-diff-check.
- **clang-tidy moved from trunk to a local-only pre-commit hook** (`tools/clang_tidy.sh`,
  the `clang-tidy` hook). trunk pinned clang-tidy 16, too old for this C++23 codebase - it
  mis-parsed and emitted false-positive fixes (const-on-mutated-local, convert-to-static,
  identifier renames) that trunk's `monitor`/export-fixes auto-applied and broke the build;
  trunk.io 403s any modern clang-tidy download. The hook resolves the hermetic clang-22
  (mirrors `clang_format.sh`), version-gates it, requires the compile DB, and reports only
  (no `--fix`). It is `stages: [manual]` (opt-in via `pre-commit run clang-tidy`) until the
  follow-ups below; promote it to an automatic gate by dropping `stages`:
  1. **Fix the generated compile DB - ROOT CAUSE FOUND, port from mbo #270 (merged 2026-08-08).**
     The abort ("too many errors" / `'concepts'` / `'time.h' file not found`) was NOT the
     `<version>`-shadowing theory. `compile_commands-update.sh` runs
     `bazel run @…//:refresh_all --config=clang`, but `--config=clang` only configures the build
     of the _extractor tool_ and never reaches the internal `aquery`, so every recorded command
     named the autodetected **Apple clang**, not the hermetic toolchain clang-tidy uses. Port mbo's
     fix: bump the extractor pin `75ba4c3` -> `6eb3ff1` (`bazelmod/dev.MODULE.bazel`; adds
     `--bcce-prefer-target-config`), replace the script to resolve the hermetic `clang++` and pass
     `--bcce-compiler=<clang++>` + `--bcce-prefer-target-config` **after `--`** (bazel eats them
     otherwise), plus Darwin-only `--bcce-copt=-isysroot$(xcrun --show-sdk-path)` (hermetic clang
     has libc++ but no system C headers); add a probe target to materialize the toolchain on a
     fresh checkout. No `--extra-arg` hack in `clang_tidy.sh` is then needed. Also fix the still-
     misspelled `.clang-tidy` `bugprone-signed-char-misuse.CharTypdefsToIgnore` ->
     `CharTypedefsToIgnore` (`WarningsAsErrors: '*'` + the header-guard disables are already done).
  2. **Add a report-only CI job** (`continue-on-error: true`) that builds the DB + runs the manual
     hook, so the ubuntu path gets exercised without gating - mirrors mbo #270.
  3. **Sweep the clang-tidy-22 finding set across `xff/`** on the now-clean parse: `misc-include-cleaner`,
     `misc-const-correctness`, `performance-unnecessary-value-param`, `concurrency-mt-unsafe` (getenv),
     `hicpp-vararg` (ioctl / exec), and the noisy new-in-22
     `cppcoreguidelines-pro-bounds-avoid-unchecked-container-access` (fires on every `operator[]`;
     mbo saw ~80% of findings from it - re-tune `.clang-tidy` for it) - fix or narrowly suppress.
  4. Once clean, drop `stages: [manual]` + `continue-on-error` so the hook gates every commit.
- **Adopt pre-commit.** `.pre-commit-config.yaml` (+ `.pre-commit/` scripts) and a
  CI `pre-commit` job: clang-format (mirrors-clang-format), shfmt, shellcheck,
  actionlint, and the local hooks (`no-do-not-merge`, `no-todos-without-context`,
  `done-gate-covers-all-jobs`, the no-em-dash check). Retire the hand-rolled
  clang-format CI step once pre-commit owns it.
- **Adopt `mbo::testing::EqualsText` for multi-line test comparisons.** The convention is now
  in `STYLE_CPP.md` / `AGENTS.md`: prefer `EXPECT_THAT(actual, EqualsText(golden))` (unified diff,
  line by line) over `EXPECT_EQ` for multi-line strings, with `WithDropIndent` /
  `mbo::strings::DropIndent` / `DropIndentAndSplit` (`@helly25_mbo//mbo/testing:matchers_cc`,
  `@helly25_mbo//mbo/strings:indent_cc`) when an indented literal reads better. Retrofit the
  existing `EXPECT_EQ`-on-multi-line tests in one sweep (e.g. `xff/render/render_test.cc`'s
  `RenderTable` goldens, plus any generated-help / man / markdown goldens), sized by count.

- **Reconcile our glob->RE2 translator with `mbo::file::Glob2Re2` (#122). RESOLVED (#333):
  deliberately keep ours, documented.** `//xff/glob:GlobToRegex` (extracted from the gitignore engine
  in #316, reused by `--regextype=GLOB`/`SHGLOB`) overlaps `mbo::file::Glob2Re2` but the `**` semantics
  differ on purpose: ours are gitignore's (`**/`->`(?:.*/)?`, trailing `/**`->`.*`, glued `**`->`*`),
  mbo's are its own (`**`->`.*`, slash-enclosed `(/.+)?` / `(.+/)?` with `.+`, gated by
  `allow_star_star`). xff walks its own VFS engine and needs only the pure pattern->RE2 step, not mbo's
  filesystem globbing (`Glob`/`GlobSplit`/`GlobEntry`), so a migration would trade a self-contained ~130
  line translator for a semantic-shim on a lib we otherwise do not use. The divergence + rationale now
  live in `xff/glob/glob.h`; no migration. (mbo's FS globbing may still be worth adopting elsewhere.)

- **Sweep for C++ move/forward oversights.** Audit the codebase for missing modern-C++ value
  idioms: a by-value sink parameter stored into a member without `std::move` (e.g. a ctor taking
  `T x` then `x_(x)` instead of `x_(std::move(x))`); a forwarding reference `T&&` passed on without
  `std::forward<T>`; a returned local that would benefit from being a move (usually NRVO handles it,
  but a returned member or subobject does not); needless copies where a `std::move` on a
  no-longer-used local applies. Note the trivially-copyable exception: `std::move` on a trivially
  copyable type is a no-op and clang-tidy's `performance-move-const-arg` flags it, so keep such moves
  only as a deliberate future-proofing idiom (with a `NOLINT` + comment), else drop them. Prefer a
  clang-tidy-driven pass (`performance-move-const-arg`, `performance-unnecessary-value-param`,
  `bugprone-move-forwarding-reference`, `cppcoreguidelines-rvalue-reference-param-not-moved`,
  `hicpp-move-const-arg`) plus a manual read of the hot constructors. Sized by finding count.

### find / xff features (roadmap tail)

The standard find predicate surface is complete (the access predicates
`-readable` / `-writable` / `-executable`, `-inum` / `-samefile`, symbolic `-perm`
modes, `-lname` / `-ilname`, and `-fstype`; all in the CHANGELOG and covered by the
engine unit test), as is the reusable markdown-table-alignment skill (#66). What
remains below is the design-forked / larger work.

- **Parallel traversal + `--jobs` + deterministic `--sort`** (#43). **Complete.** A worker-pool
  walk (`ReadPool`, `absl::Mutex`; parallel `readdir`+`lstat` on workers, single-thread
  coordinator/visitor) with `--sort=none|dir|subtree|tree` (`absl::c_sort`), `-j N` / `--jobs=all`,
  mode-scoped defaults, unit-tested across worker counts plus a tsan CI cell. The CLI gap closed
  with `xff/cli/sort_test.sh` (every mode walks the whole tree; `--sort=tree` is a deterministic
  global order identical across `-j`; `--sort=dir` orders each directory) - #43/#27 done.
- **Exit-code model refinement + `--skip-unsupported` + impossible-task-fail**
  (#44). Shipped: (a) match-sensitive exit -- the default stays find semantics
  (0 ran / 2 error, match status never affects exit), while `--quiet` (suppress
  output, exit by match) and `--exit-match` (keep output, exit by match) make
  "1 = no match" reachable; an error still outranks match status (exit 2). (b)
  impossible-task-fail -- a predicate that cannot be evaluated correctly on an
  entry's FS (e.g. `-Btime` where birth time is unrecorded) is a hard error
  (exit 2), reported once; `--skip-unsupported` downgrades it to a warning + skip.
  (c) `-q` -- the grep-compatible short alias of `--quiet`, a special-cased global
  (like `-h`/`-help`/`-version`), self-documented via the globals table. Nothing
  outstanding except, if a concrete case ever appears, extending impossible-task
  detection beyond birth time (only `-Btime`/`-Bmin`/X=B `-newerXY` flag it today;
  a Y=B reference with no btime stays a silent no-match by design).
- **`--exact` + `--path-encoding`** (#45). **Both shipped.** `--exact`: the default
  is the **filesystem-native, naturally-expected** behavior - the xff style matches
  `-name` / `-path` the way the entry's own volume resolves names (case-insensitive
  on a folding FS like APFS / HFS+ / NTFS, case-sensitive on ext4 and friends), so
  most users get what they expect on their platform; **`--exact` opts out** to force
  verbatim byte-for-byte comparison regardless of the FS, and the find style stays
  byte-exact (drop-in faithful). Backed by a `vfs::FileSystem::IsCaseSensitive`
  probe (`pathconf(_PC_CASE_SENSITIVE)` on macOS/BSD; conservative case-sensitive
  fallback on Linux and when unprobeable), cached per device during the walk. Scope
  is **case only**; NFC/NFD normalization and fuzzy matching (the earlier
  `--exact+`/`--exact-` sketch) stay deferred. Linux per-directory casefold (ext4
  `+F` / `statx STATX_ATTR_CASEFOLD`) is a later refinement. `--path-encoding=raw|escape`
  also shipped: the plain renderer C-escapes backslash + control bytes under
  `escape` (kNul stays raw, kJsonl always JSON-escapes; `raw` = find-compatible
  default, `escape` = C-style `\xNN`, like `ls -b`).
- **`--feature=NAME` / `--feature=no-NAME` capability gates** (#73). **Parked** - no
  concrete customer yet (valued knobs like `--implicit-print` / `--capture-override`
  / `--exec-fields` are dedicated flags by design, and whole-behavior switches are
  `--config` styles), so building it now is infrastructure without a user. **Design,
  ready to build the moment a boolean capability appears:** repeatable on/off dials
  resolved after the style/config defaults and before explicit dedicated flags
  (explicit wins); a **feature registry mirroring the descriptor/globals SOT** (each
  feature = name + one-line summary + default-per-style) so unknown `--feature=X`
  errors and `--help` / `--man` / `--markdown` list features automatically;
  `--explain` shows each feature's resolved value + origin; a style is just a named
  bundle of feature defaults (design-config.md L162-165).
  - **VERDICT (2026-08-13): do not build the general registry; the trigger was wrong.** The rule said
    the first boolean user-toggleable capability must become the first `--feature`. Since then xff has
    shipped 22 boolean globals (`--archive-extract`, `--archive-delete`, `--archive-any`,
    `--exec-fields`, `--dry-run`, `--safe`, `--exact`, `--hidden` / `--no-hidden`, ...) and not one
    of them wanted the mechanism: each read better as a NAMED MEMBER of its family (the
    `--archive-*` set, which `--help=archive` then gathers) or as a VALUE on an existing flag
    (`--case=smart`, not `--smart-case`). Two spellings for one switch is a real cost, and the
    registry's payoff needs several customers it never got.
  - **ADOPTED INSTEAD (user, 2026-08-13): `--unstable=NAME[,NAME]`.** The one case that remains
    compelling is gating UNSTABLE features so their spelling is not a promise - `-fuzzy` ranking
    (#168) and the FUSE mount (#183) are exactly features worth shipping before their flag names are
    settled. One list beats a capability namespace that looks permanent. Design, to build the first
    time a feature needs it:
    - **repeatable and comma-separated**, resolved once before the walk; an unknown name is a usage
      error naming the known set, exactly as a bad `--summary` value is (never a silent no-op);
    - **a registry mirroring the globals SOT** - name + one-line summary + what it gates - so
      `--help=unstable` lists them without a hand-maintained page, and `--explain` shows which are on;
    - **every gated surface says "unstable" in its own help**, so a user reading `--help=NAME` for
      the flag it enables cannot miss that it may change or vanish;
    - **graduation is deleting the gate,** not adding an alias: when a name settles, the flag becomes
      ordinary and the `--unstable` entry disappears (with a CHANGELOG line, since a run that named
      it then errors - which is the honest outcome for something documented as subject to change);
    - **it gates SPELLING, never safety.** A destructive capability stays behind its own explicit
      flag (`--archive-delete`); `--unstable` is not a way to arm something dangerous by list.
  - **The other two cases are covered or not yet real.** Post-1.0 behaviour-migration windows
    (`--feature=no-size-round-up` while a default changes) are a genuine use, but xff has no legacy
    to carry before 1.0. Admin capability DENIAL is already the config `[policy]` tier's job, which
    knows flag names and needs no parallel capability namespace.
  - **So:** AGENTS.md's trigger is replaced by the rule the practice validated (prefer a named family
    member or a value on an existing flag), and `--feature` stays unbuilt. Revisit it after 1.0 if
    migration windows accumulate; build `--unstable=` instead the first time a feature needs to ship
    without committing its name.
- **Grow `xff/datetime` into a parse+format lib** (#70): named formats, field
  modifiers, and the `--time-format` / `--timezone` global flags have shipped, as
  have the last deferred pieces -- the `--tz` short alias and fixed-offset zone
  specs (`+05:30` / `-0800` / `+01`). Nothing outstanding here.
- **Mode mechanism** (#54). **Subsumed by `--config`:** `--config=find|xff` is the
  style/mode selector (#72) and `argv[0]` dispatch picks the default (#59);
  design-config.md L159 deliberately folds `--style` / `--mode` into `--config`, and
  `--config=xff:2` gives version epochs a binary `--modern` cannot. No separate
  `--mode` flag; the `--modern` umbrella stays deferred.
- **Full help system** (grow the `--help` overview shipped in #171). The CLI is
  flag-only -- no subcommands (decided 2026-06-28; xff is a single-purpose tool like
  fd/ripgrep, so find and xff keep one grammar). Shipped (#184): `--help=NAME` topic
  help and `--help=`/`=list`/`=all` index, both read from `registry::All()` + the
  per-descriptor `summary` (#181); GNU-compatible `-help`/`-version`; and a guiding
  error when a `help`/`version` operand is typed out of git habit (xff flavor).
  Remaining:
  - **Global-flag and config topics.** `--help=NAME` covers expression primaries
    today; extend it to global flags (`--help=--config`, `--help=--sort`, ...), which
    needs the globals enumerated the way `registry::All()` enumerates primaries (the
    globals are not in the registry yet).
  - **Explain the config system + flavor selection in depth - SHIPPED.** `--help=config`
    covers the layered tiers (system < user < `--xffrc` < CLI, later wins) and their
    precedence, that there is no project/ancestor `.xffrc` discovery (#119), `--no-config`,
    the `--xffrc` NON-ARMING rule + `--allow-exec` trusted-tier arming, and flavor selection
    by `--config` and by the `argv[0]` invocation name (a `find` symlink runs strict find, `rg`
    the rg style; any other name activates a same-named config block over the xff default). The
    config flags are pulled from the globals SOT via the `config` topic tag (like `--help=stats`),
    so the flag list cannot drift; points at `--help=styles` for the per-style defaults table.
  - **Generated reference docs from the same registry SOT.** Drive docs off
    `registry::All()` (+ the globals table, once enumerated) rather than maintaining
    parallel copies:
    - **Man page on demand**: emit roff/troff (so `man -l -` / a packaged `man xff`
      works) via a flag, e.g. `--man` / `--man=TOPIC`, generated at runtime from the
      registry + the global-flag table + the config docs.
    - **Integrated Markdown documentation build**: emit a `.md` reference of all
      primaries / flags from the same source (an integrated mode and/or a build
      target; a separate external generator alongside the man-page builder is fine if
      need be). Wire it into CI so the committed docs cannot drift from the vocabulary.
    - **One walk, native renderer per format (#125, A/B/C).** `--man`, `--markdown`,
      and `--help=full` were three hand-rolled walks over the same SOT that had drifted
      (man/markdown lacked the per-item `.details` and every sub-vocabulary topic that
      `--help=full` carried). Fixed by a single `WriteReference(DocRenderer&)` traversal
      (`xff/cli/doc_renderer.{h,cc}`) driving a format renderer: `Document`/`Section`/
      `Subsection`/`Prose`/`Bullets`/`Entry`/`Rows`/`Example`/`SeeAlso`, plus a shared
      `WriteMarkdown()` that understands a Markdown subset (`#`/`##` headings, `- `
      bullets, blank-line paragraphs, backtick `code`) so authored prose renders
      natively in every format. **PR A (done):** `RoffRenderer` - `--man` now carries the
      complete reference (options + expression with details, FIELDS incl.
      braces/namespaces/qualifiers, PRINTF/TIME/SIZE, EXAMPLES, EXIT STATUS, extended
      SEE ALSO) + a `doc_renderer_test` drift guard on the in_full topic set.
      **PR B:** `MarkdownRenderer` over the same walk. **PR C:** `PlainRenderer` for
      `--help=full` (retire the bespoke `FullReference` + the main.cc topic renderers).
    - **Committed `XFF.md` reference + drift guard - SHIPPED.** `XFF.md` at the repo root is the
      verbatim `xff_full --markdown` output, checked in as the browsable full reference (there is no
      README manual; the new `README.md` is a short overview that links to it). It is generated, not
      hand-edited: `./xff-md-update.sh` rewrites it and `//xff/cli:xff_markdown_test` (a `diff_test`,
      XFF_FULL_ONLY so it runs under `--config=xff_full` in every CI test job) regenerates and fails
      on any drift. No auto-update pre-commit hook: regenerating needs the full `xff_full` build
      (pcre2 / archive extras), too heavy for a git hook - the CI diff_test is the gate, the script
      the one-command fix (same split as `compile_commands-update.sh`). Also added a config-adaptive
      `//xff` alias: it resolves to `//xff/cli:xff_full` in a full build and the lean `//xff/cli:xff`
      otherwise, keyed on a single `//xff:full_build` `config_setting_group` that `XFF_FULL_ONLY`
      (xff_full's own compatibility gate) also uses - so "full mode" is defined once and a future extra
      (archive #83) only edits that group. CI runs the guard as a fast
      pre-flight `xff-md` job (builds only `xff_full` + the diff_test, repo-cache-only); the heavy
      matrix (`test` / `tsan` / `minimal`) `needs: [pre-commit, trunk, xff-md]`, so a stale reference
      or a lint failure fails in minutes instead of after the full asan build. Also dropped the macOS
      asan cell (Linux asan is enough sanitizer coverage): the matrix is now ubuntu default + ubuntu
      clang-asan + macos default.
  - **`--help` readability + discoverability** (2026-07-04 feedback):
    - **Blank line before each section header** (`Traversal:`, `Matching:`, ...) in the
      `--help` overview, so the groups are visually separated.
    - **A full, detailed expression reference - SHIPPED (sweep complete).** `registry::Descriptor`
      gained an optional `details` field (the per-primary counterpart of `GlobalFlag.details`);
      `RenderOne` shows it in `--help=NAME` and `--help=full` (`--help=expressions` stays
      summaries-only). Now populated across every non-trivial primary: the exec/capture cluster,
      -delete/-prune/-quit, -regex/-iregex, -size, -diff/-hash, -mime/-lang, -ls/-printf, the
      time-comparison family (-mtime/-mmin/-atime/.../-daystart), the matching predicates
      (-name/-path/-lname globs + -content/-rxc), the attribute tests (-type/-xtype/-perm/-fstype/
      -empty/-sparse/-readable/-writable/-executable), the output actions (-print/-print0/-println/
      -printfln/-grep + the -fprint family), the reference-time predicates (-newer + the -newerXY
      matrix anchor + -newermt), and traversal/owner/operators (-maxdepth/-mindepth/-depth/-xdev,
      -uid/-gid/-user/-group/-nouser/-nogroup, -a/-o/-not/-xor). Only self-explanatory synonyms
      (-wholename/-and/-or/!/-d/-mount/-x) and -true/-false stay summary-only by design.
    - **Worked-examples cookbook - SHIPPED.** `--help=cookbook` (aliases examples / recipes), folded
      into `--help=full` via `in_full`: task-oriented recipes (largest files, disk-use-per-ext,
      safe stale-file delete, language-filtered content search, the git-blame author-line-counts
      -exec pipeline, a sha256 manifest, recently-changed-as-jsonl) built from a `Recipe` SOT, each
      with a runnable command. Note: per-line author aggregation is an -exec + shell pipeline, not
      `--summary` (which reduces over matched files, not lines within them).
    - **Surface the format / placeholder vocabulary.** The `{field}` template vocabulary,
      `-printf` `%` directives + the `%{field}` escape, and the qualifiers (`:s/PAT/REPL/`,
      path-component, time) are documented nowhere reachable from `--help`; add a topic
      (e.g. `--help=fields` / `--help=format`).
    - **A top-level map of the help system** in `--help`: state what it supports -
      `--help`, `--help=TOPIC`, `--help=list`, `--help=expressions`, `--man`, `--markdown`,
      `--explain` (and any `--help=full`) - so users can find the detailed views.
    - **A flavor feature-map** (2026-07-04 feedback): a find/xff/rg x
      `[behavior] [controlling flag] [find] [xff] [rg] [current]` comparison table,
      rendered from ONE static per-style-defaults config the resolvers also read (so it
      cannot drift) - the #103 config x style matrix made concrete. The `current` column is
      a per-behavior `--explain`. Sequence after smart-case so its rows are complete.
    - **Worked examples / a cookbook** (2026-07-04 feedback): `--help` should carry
      concrete recipes, not just a flag list. Motivating example - per-file `git blame`
      author line-counts: run `git blame` per file, capture the authors and their line
      counts, then aggregate with `--summary` (distributions / totals). Exercises
      `-exec`/`-capture` + the field vocabulary + `--summary` end to end.
- **Extended logical operators**: shipped. `-xor` / `-nand` / `-nor` / `-xnor` are
  xff extensions (find has only `-a`/`-and`, `-o`/`-or`, `-not`/`!`), with the
  conventional precedence `NOT > AND/-nand > XOR/-xnor > OR/-nor`; the strict find
  style rejects them. (`-xor` matches exactly one side; the rest are the negations
  of and/or/xor.)
- **Line count as a first-class metric** (2026-07-04): **the `{lines}` field shipped** - a
  per-text-file line count in the field vocabulary (`{lines}`, `-printf` `%{lines}`, `--template`),
  `wc -l`-style but also counting a final unterminated line; empty for a binary / unreadable /
  non-regular file (`content::FileLineCount` + `CountLines`, reusing the grep NUL-byte binary
  heuristic). **Remaining:** surfacing it as an aggregate (sum + a distribution across matches),
  which is the `lines` metric of the histograms work (#81), not a separate item.
- **Hash-verification workflow (#109) - DONE (single-pass tally deferred).** The hashing primitives
  (#105) and now the `-hasheq EXPECTED` matcher are in: `-hasheq` computes the file's digest and is
  true when it equals EXPECTED, a `{field}` template rendered per entry (so `-hasheq {def.SUMS}`
  checks a sidecar value and `! -hasheq …` selects drift); `-hasheq=ALGO[/ENCODING]` shares the
  `-hash` spec grammar, and hex comparison folds case. **Dedup grouping shipped** as the first-class
  `--summary=hash` mode (identical files collapse into one bucket; also spellable `--summary={hash}`).
  **Deferred refinement (single-pass tally):** a one-pass verified-vs-failed count. Both viable
  designs (a `{hasheq}` verdict field feeding `--summary`, or run-level ok/fail counters) converge on
  stashing the per-entry `-hasheq` verdict into the reduction feed, which must be done thread-safely
  for the parallel walk (the tsan cell). Not worth a half-baked version: the two-run idiom
  (`-hasheq` / `! -hasheq`) plus `--summary=hash` already covers the workflow. Build the single-pass
  tally when a concrete need appears, wiring the verdict through the same per-entry reduction feed the
  summary/histogram sinks use. **Deferred producer:** a sidecar-manifest reader that populates
  `{def.X}` from a `sha256sum`-style file, so `-hasheq` needs no bespoke manifest parser.
- **Smart-case matching (#116) - SHIPPED as a `--case` value, not a boolean flag.** The rg / fd
  convention (an all-lowercase pattern folds case, any uppercase forces case-sensitive) is
  `--case=smart`, with the short spellings `-s` / `-s+`; `-s-` / `--case=sensitive` and `-i` /
  `--case=insensitive` are the other two modes. The rg style defaults to smart, find / xff to
  sensitive (`ResolveCaseMode`). It deliberately did NOT become a `--smart-case` boolean: case is one
  three-valued setting, so it stays a valued flag (the same reasoning that keeps `--feature` unbuilt).
  It applies uniformly to `-name` / `-path` / `-regex` and the content matchers; `--exact` still
  forces byte-exact matching, since that is the FS-encoding escape hatch and outranks the case mode.
- **`-mime` / `-lang` vocabulary: richer per-type data + table overrides - deferred.** Matching is
  now always case-insensitive (MIME type/subtype names are case-insensitive per RFC 2045/6838;
  language names keep a canonical case for the `{mime}`/`{lang}` display), independent of
  `--case`/`-i`/`-s` - shipped as a lower-cased glob compare in `EvalMime`/`EvalLang`. **Deferred
  (build when a consumer or the override feature gives it a concrete driver):** turn the
  `TypeForName`/`LanguageForName` return into a `{key, data}` struct - `key` the canonical
  lower-cased value (the match target), `data` an extensible payload for `file(1)`-style details
  (description, category, linguist color / aliases). The tables would become canonical vocabularies
  keyed on the lower-cased value (each entry once), with a runtime-derived `ext -> key` index
  (uniqueness-checked: one ext maps to one entry; true multi-candidate disambiguation is the
  separately-deferred content-classification feature) and the ability to override the compiled-in
  tables at runtime. Callers already reach the vocabulary only through the query, so the storage
  change stays behind the API. The design is captured in the NOTE in `xff/mime/mime.h` and
  `xff/language/language.h`.
- **EPIC: Sharded-file support (#84) - DESIGNED (2026-08-05), building v1 autonomously.** Collapse a
  shard set (`data-00000-of-00010`, `foo.tar.001` parts, `arc.z01`/`arc.zip`, ...) into one logical
  entry. Full spec in [`docs/design.md`](design.md) "Sharded files": off-by-default `--shards`; v1 =
  **display + stats only** (matching / actions stay per real shard); six-axis scheme model; capture
  vocabulary `stem` / `index` / `total` / `dup`; built-in catalog + `--shard-pattern=REGEX`;
  `--shards-show` / `--shards-dedup` policy; completeness by distinct index; sharding is not rotation.
  Reassembled-content view is v2 (post-#83, on the archive vfs backend). Slices:
  - **A - `xff/shard` engine + capture vocabulary.** Pure `(stem, index, total?, dup?)` parse of one
    filename against a scheme; the six axes; the built-in catalog defined via the capture vocabulary
    (incl. the special-boundary schemes `zip-split` / `rar-old` / `rar-part`). Library + unit tests,
    no CLI / traversal wiring.
  - **B - grouping + completeness + dedup.** Given a directory's entries, group by set identity, dedup
    on `dup` (policy), compute completeness against distinct indices, emit logical shard-set records.
  - **C - CLI `--shards[=auto|SCHEME,...]` + walk integration.** Enable + scheme selection (default
    off; `auto` = whole catalog); collapse the listing to one line per set. bashtest / golden.
  - **D - policy flags + completeness surfacing.** `--shards-show=wildcard|first|count`,
    `--shards-dedup=first|mtime|error`, and the `f-???-of-003 (2/3 - INCOMPLETE)` output.
  - **E - custom pattern `--shard-pattern=REGEX`** (named captures; repeatable) - the escape hatch.
  - **F - stats integration.** `--summary` / `--histogram` aggregate per logical set (count + size);
    `{shards}` set-count field.
  - **v2 (deferred, blocked on #83):** reassembled-content virtual view so `-grep` / `-content` /
    `-hash` / `-size` see the concatenated whole, reusing the archive vfs backend.
- **Respect `.gitkeep` in gitignore handling (#120) - SHIPPED (2026-07-07).** A `.gitkeep` is a
  pure convention (git itself has no notion of it) that keeps an otherwise-empty directory in a
  repo. Decided: **always on** (no separate mode) - when gitignore handling is active, a `.gitkeep`
  is never ignored by the gitignore layers, as if by a top-precedence `!.gitkeep`, so a directory
  kept in the repo by its `.gitkeep` always surfaces it. Implemented in `IgnoreStack::Decide`
  (`xff/engine/run.cc`): a `.gitkeep` short-circuits the gitignore / repo-exclude layers but still
  runs through explicit `--exclude` / `--include`, so a CLI exclude can still override it.
- **Skip VCS metadata (`-g` drops `.git`; then `--skip-vcs`).** SHIPPED (git slice): when gitignore
  handling is active (`-g`, or auto in a repo), the `.git` directory (and the `.git` gitlink file a
  submodule / worktree uses) is pruned at any depth, like ripgrep / fd - git never lists `.git` in a
  `.gitignore`, so the rules alone never dropped it. Deliberately independent of `--hidden`, so the
  user's own dotfiles (`.bazelrc`, `.gitignore`) still show; only git's plumbing goes. In
  `xff/engine/run.cc`'s Walk callback, gated on `gitignore_on`.
  - **`--skip-vcs[=LIST]` (#131) - SHIPPED.** Dir-pruning generalized to all known VCS:
    `.git` / `.hg` / `.svn` / `.jj` / `.bzr` / `_darcs` / `CVS`. Bare (or `=all`) = all; `--skip-vcs=git,hg`
    = an explicit, frozen subset (adding a VCS to the default set later never changes an explicit
    invocation's results); `--no-skip-vcs` / `=none` = off; an unknown token is a usage error (exit 2).
    Independent of `--hidden` and of ignore-rule interpretation. `-g` implies `--skip-vcs=git` (the
    git slice); an explicit `--skip-vcs=...` overrides; default off otherwise (find-compat). Tokens:
    `git,hg,svn,jj,bzr,darcs,cvs`. `ResolveSkipVcs` in `xff/engine/run.cc` (last-occurrence-wins);
    the Walk callback prunes by the resolved name set.
  - **`--ignore-vcs` / `--no-ignore-vcs` (#132) - SHIPPED.** The rg-style toggle for VCS-provided
    ignore _files_ (a different axis from `--skip-vcs`'s dirs). `--no-ignore-vcs` drops the VCS
    ignore-file layer (`.gitignore` + `.git/info/exclude` + global git excludes; later `.hgignore`)
    while keeping `.ignore` / `.xffignore`; `--ignore-vcs` respects it. Implemented as synonyms in the
    one gitignore ternary (`ResolveGitignoreMode`, `xff/engine/run.cc`): `--ignore-vcs` == AUTO (like
    bare `-g`), `--no-ignore-vcs` == OFF, both last-occurrence-wins participants alongside
    `-g`/`--gitignore`. Precedence settled as last-wins rather than a fixed `--no-ignore-vcs > -g`
    priority, to match the existing gitignore-flag convention (`-u`/`--no-ignore` stays the
    position-independent master-off over every ignore source). `.ignore` / `.xffignore` are a separate
    axis (`--ignore-files`), untouched - which is exactly what distinguishes `--no-ignore-vcs` from
    `-u`. Today git is the only VCS ignore file, so `--no-ignore-vcs` is nearly `--gitignore=off`; the
    names earn their keep once xff reads non-git VCS ignore files.
- **`--` globals are position-independent (#145) - SHIPPED.** A double-dash global may now appear
  anywhere - before the roots, among them, or in the expression, including the tail
  (`xff . -type f --summary=ext`) - killing the "unknown predicate: '--summary=ext'" WTH moments.
  Safe because every primary/operator is single-dash, so a `--`-token at a primary/operator boundary
  is unambiguously a global; `ExprParser::SkipGlobals()` hoists it there (and the roots loop hoists
  between roots), while a `--flag` inside a primary's argument run (an `-exec` command, a `-printf`
  format) stays a literal argument - never stolen from a child command. A bare `--` ends option
  parsing and disables hoisting; single-dash globals stay leading-only (ambiguous with primaries).
  Decided FULL permutation (any `--` global) over an output-only allowlist, since `--top`/`--histogram`
  and any future output global all fall out of the one rule. Grammar-affecting `--regextype` still
  belongs before the expression (a late one is hoisted but does not retro-recompile matchers). The
  cookbook/README/usage examples now show `--summary` at the tail; #144 (the `-summary`-as-action
  idea) is superseded for its positional driver.
- **`--summary` is repeatable (#144) - SHIPPED.** Each `--summary[=X]` is now an independent sink
  (like `--histogram`, already a list), so `xff . -type f --summary=ext --summary=type` prints both
  tables, in order, blank-line separated; `--summary=none` clears the list. Delivers the multiple-sinks
  benefit that was #144's only remaining driver after #145 gave the positional win - WITHOUT the
  expression-action machinery I first over-scoped (no single-dash action, no suppress-default-print,
  no expression-scoping). `ResolveSummaries` returns a `vector<SummarySpec>`; the walk accumulates one
  `{group -> {count,size}}` map per sink and a render lambda emits each table. `--top` /
  `--summary-precision` stay global and apply to every table (per-sink modifiers deferred). Mirrors the
  existing `--histogram` list exactly, so it was a bounded change, not the aggregation-core refactor I
  wrongly estimated.
- **Color support**: `--color[=auto|always|never]` ships an `ls`-like scheme keyed
  on the filesystem file type (directory, symlink, executable, fifo/socket/device);
  auto colors only a tty and honors `NO_COLOR`. Still open: per-language coloring
  keyed on `languages.yml` (the same data source `-lang` / `{lang}` already load).
- **`-cmp` / `-diff` (compare each match against a per-entry target).** The target path
  is built per entry from the field vocabulary (`{def.B}/{relpath}`, ...), so comparing a
  whole tree against a parallel one is `xff A -type f ! -cmp '{def.B}/{relpath}'`. The find
  expression is how you control which files are compared. Ratified split (2026-07-03);
  polarity **TRUE = same** (like `cmp`/`diff`, exit 0 = identical):
  - **`-cmp TARGET`** = pure byte-exact matcher (a TEST). **SHIPPED (#231).** `! -cmp`
    lists changed files; a missing/unreadable target differs (-> false); never normalizes.
  - **`-diff[=STYLE] TARGET`** = a diff-producing ACTION that also returns true/false
    (silent + true when equal; emits + false on a difference). **SHIPPED** via `mbo::diff`
    (0.13.0). STYLE picks the mbo output: `u[N]` unified (default `u3`), `c[N]` context, `n`
    normal, `y[N]` side-by-side, `none` = compute-but-silent matcher. `--diff-algorithm=`
    `naive|direct|myers` (default myers) selects the engine. Text only; a binary side prints
    `Binary files A and B differ` to **stderr** (byte compared). The header carries each side's
    mtime (`diff -u` style).
    - **Normalization SHIPPED:** `--diff-ignore=<tokens>` where a token is `ws` (all whitespace),
      `change` (whitespace changes), `trail` (trailing whitespace), `blank` (blank lines), `case`
      (letter case), or `eofnl` (a missing final newline), comma-separated; plus
      `--diff-ignore-matching=REGEX` (RE2, ignores matching lines). Both validated before the walk
      (an unknown token or bad regex is a usage error, exit 2) and shared with the apply path via
      `ApplyDiffIgnore`. The non-copyable RE2 option is sidestepped by building a fresh
      `DiffOptions` per `-diff` entry (`emplace` per call, with `log_errors(false)`). There is no
      `lead`/`eol` token: leading whitespace is subsumed by `change`/`ws`, and CRLF-vs-LF by `trail`
      (a `\r` is trailing whitespace).
    - **Git-style header SHIPPED:** `-diff` sets `time_format=""` so the header omits the per-file
      mtime (`--- a/one.txt`), making the output reproducible; the golden tests no longer strip a
      timestamp with `sed`. (mbo `ignore_missing_final_newline` + empty-`time_format` landed in
      helly25/mbo#234.)
    - **Still deferred:** making `--diff-ignore*` `.xffrc`-settable (the config pass). Full design in
      the memory note (`project_xff_cmp_diff`).
    - **`mbo` dependency:** built against a `git_override` pinned at the mbo `main` commit merging
      helly25/mbo#234 (0.13.0-dev: `mbo/diff` + `mbo/digest`); drop it for a plain `helly25_mbo`
      0.13.0 bump once that releases to BCR.

- **`--explain` flavor table: two-tier layout - SHIPPED (2026-07-06).** `RenderFlavorTable` now
  leads with the facets that vary ("Where the styles differ:" for `--help=styles`, "Relevant to
  this run:" for `--explain` - the latter also promotes any facet a flag overrode this run), then a
  "Same in every style:" section for the rest. Still generated from `engine::FlavorFacets()` (a
  presentation layer over the same SOT). Note: with today's five facets all differing across styles,
  the "Same in every style:" section is currently empty - it auto-populates as uniform facets are
  added (e.g. behaviors a future `--feature` gate introduces).

- **`xfd` dropped (2026-07-06): rg is the single opinionated style.** `xfd` was identical to `rg`
  (both: gitignore + skip-hidden + smart-case opinionated), so it was removed rather than aliased
  (an alias silently using another config is confusing). There is no `kXfd` style: `--config=xfd`
  and an `xfd`/`fd` invocation are now just plain names (named-config selectors on the xff base, no
  magic remap). **Reintroduce only if given a genuinely distinct fd direction** (regex-by-default
  bare pattern, its own default action / output) that earns a separate name; today nothing in the
  unified grammar distinguishes it from `rg`.

- **Byte units: SI vs binary - human output default resolved to SI (2026-07-06).** The only
  unit-suffixed OUTPUT is the human-size renderer (`format::Size`, `--summary` / `-ls`), and it
  already spells both scales correctly: SI `kB`/`MB`/`GB` = 1000^N (lowercase SI kilo), IEC
  `KiB`/`MiB`/`GiB` = 1024^N. `--human` now defaults to **SI** (bare `--human` and the xff/rg style
  default; `--si` is an alias; `--human=iec` / `=1024` selects binary, `=si` / `=1000` decimal,
  `=off` raw), since IEC's `i` reads less human. No site mixes the two.
  - **Still open (audit, not a known bug):** the INPUT unit grammars stay find-native binary and
    are not renamed - `-size` / `-blocks` `k`/`M`/`G`/... (find-compat: `k` = 1024), `--block-size`,
    and `--buffer`'s own `B`/`MB`/`MiB` grammar. These are parsed, never printed with a suffix, so
    there is no "MB for 1024^2" mismatch; a future pass could offer explicit `KiB`-style input units
    for xff-style callers and document the rule in `--help=size`.

- **Config: drop the project `.xffrc` layer entirely (Option B, decided 2026-07-06).** No
  auto-discovered project config at all - not the ancestor cascade, not subtree scoping. Config
  comes from three tiers only: **system** (`/etc/xff...`, root-owned - defaults + a policy that
  can hard-deny capabilities), **user** (`~/.config/xff/...`, trusted-as-user), and an **explicit
  `--xffrc=FILE`** (the user names the file to load it). Per-directory _ignore_ rules stay in the
  ignore family (`.gitignore` / `.xffignore`) - that is ignore, not config, and is unaffected.
  Removes: the `.xffrc` cascade discovery (`loader.cc`), `ProjectConfigMode` + `--project-config`,
  and the project branch of the policy gate; simplifies the system layer (its old job of capping
  the untrusted project layer is gone). Reverses the `design.md` §149 / `design-config.md`
  subtree-scoped-project intent (docs rewritten in the build).
  - **`--xffrc` arming restriction (no self-authorization).** A named `--xffrc=FILE` can no
    longer arm its own dangerous directives (reverses `loader.cc:98` "arm into the user layer").
    Driven by the existing `registry::Safety` classes: `kNone` (safe) directives are honored from
    any tier including `--xffrc`; `kSafety` (destructive) / `kSecurity` (sensitive: `-exec` /
    `-execdir` / `-ok` / capture) directives loaded from a `--xffrc` file are **inert unless
    armed**. Arming is a dedicated flag (`--allow-exec`) honored from the **CLI or the trusted
    user/system tiers, never from a `--xffrc`-loaded file**; the **system policy can hard-deny**
    even the CLI arm. An unarmed dangerous directive is inert + a one-line stderr warning.
    `-delete` keeps its own `--safe` / `--dry-run` guards (#40).
  - **SHIPPED (both slices).** (1) design-doc supersede banner + record. (2) Removed the project
    layer + `--project-config` (Source lost kProject, ConfigInputs lost `project`, loader dropped
    the cascade, policy is deny-only; a local `.xffrc` in the tree is inert). (3) `--xffrc` is its
    own tier (`Source::kXffrc`, precedence user < xffrc < cli). It is NON-ARMING: a sensitive
    (`-exec`/`-execdir`/`-ok`/`-capture`) or destructive (`-delete`) line loaded from an `--xffrc`
    file is inert (dropped + one-line warning) unless armed by `--allow-exec`, which is honored
    only from a trusted tier (CLI, or user/system config via `ArmedFromTrustedTier`) - never from
    an `--xffrc` file itself - and the system `[policy]` can still hard-deny an armed line.

- **Archive diving (#83, `--archive`): libarchive, decided 2026-07-06.** Descend into archives and
  match/list their members as virtual paths via a read-only `vfs::FileSystem` backend, so the whole
  predicate/action set works unchanged. Engine is **libarchive** via its BCR module
  (`bazel_dep(name = "libarchive", version = "3.8.1.bcr.2")`): no vendoring, less code than
  hand-rolling, and it covers tar/zip/cpio/ar/iso plus the gz/bz2/xz/zstd/lz4 filters behind one
  streaming API.
  - **Two build variants planned:** _minimal_ (tar + gz + bz2, with xz/zstd/lz4/mbedtls disabled at
    libarchive's build config) and _extended_ (adds xz/zstd/zip/...). The license/NOTICE footprint
    scales with the enabled codec set.
  - **NOTICE obligations, all permissive but must be maintained.** libarchive's closure adds bzip2,
    lz4, xz, zlib, zstd, mbedtls. Net-new license types over our Apache-2.0 / BSD-3-Clause baseline:
    BSD-2-Clause (libarchive, lz4), Zlib, bzip2-1.0.6, 0BSD (xz, no notice needed). Two are
    dual-licensed, so pin the permissive arms: zstd -> BSD-3-Clause, mbedtls -> Apache-2.0, and link
    lz4's library (BSD-2), never its GPL-2.0 CLI. With those pinned there is no copyleft.
  - **Control surface `--archive[=none|roots|all]` + `-z`, RATIFIED 2026-08-05.** Diving into a NAMED
    archive root and diving into archives met MID-WALK are separately-wanted behaviours, so this is
    one ordered enum (`none` subset `roots` subset `all`), not a boolean. Bare `--archive` = `all`;
    `-z` carries the chmod-style suffix signs (`-z-` none, `-z` roots, `-z+` all) like the `-g`
    trio. Flavor defaults: `find` -> `none` for drop-in fidelity, every xff-family flavor ->
    `roots`, because pointing xff AT an archive implies looking inside while silently descending
    every archive in a tree is a cost to opt into.
  - **Member-path spelling is a FLAG, SHIPPED 2026-08-10/11.** No single convention exists (`!` for
    JAR / Java URLs, `#` for fragments, the multi-character `!/` and `#/`, plus URI forms), so it is
    a presentation choice: `--archive-separator=STRING` (default `!`) and
    `--archive-prefix=[URI|STRING]` (default empty). `@xff_extras_api//:member_path_cc` implements
    it, hosted in the shared API module because both sides need it - the extra renders, the core
    parses a member path handed back in - and an extra must not depend on the core.
  - **The spelling rules, each test-pinned.** Rendering is plain concatenation, so an ABSOLUTE stored
    member keeps its leading slash (`a.tgz!/rooted`, and the correct doubled `a.tgz!//rooted` under
    separator `!/`) - xff adds and removes no slash, because hiding an absolute member would hide the
    Zip-Slip red flag. Splitting cuts at the FIRST separator and takes the remainder verbatim, so a
    printed path round-trips. An empty separator never matches. A prefix is strict in both
    directions, so the two spellings never silently interchange. `URI` is the one keyword (ALL CAPS,
    matching RE2 / PCRE2 / GLOB) and renders a well-formed URI: `archive:///abs/a.tar!x` for an
    absolute container (empty authority, as `file:///...`) and the opaque `archive:a.tgz!x` for a
    relative one - a blanket `archive://a.tgz` would parse the container as a HOST NAME. Any other
    prefix value is literal; there is deliberately no `none`, which would be indistinguishable from a
    literal prefix spelled `none`.
  - **A bare `/` separator is legitimate, just context-dependent.** A walk is never ambiguous: it
    meets `a.phar` as a real FILE, sniffs it, descends, and one path cannot be both file and
    directory. Only string-only splitting needs help, and even that works without the filesystem by
    scanning for a known container EXTENSION - which is how PHP resolves
    `phar:///path/a.phar/inner`, and why `.phar/` is self-marking. `SplitMemberPath`'s oracle
    overload covers all three sources (walk knowledge, stat + sniff, or a purely lexical extension
    test); the string-only overload refuses an all-slash separator rather than cutting at the leading
    slash. The marker separators are heuristics too - a directory really can be named `foo!` - they
    simply fail to be archives, so a walk finds nothing and the marker was only ever a convenience.
  - **VFS backend SHIPPED (#455): `ArchiveFileSystem` over one container, read/stat only.** 14 tests
    over a tar the test writes itself pin the decisions: implicit parent directories ARE synthesized
    (the fixture stores `dir/sub/deep.txt` with no `dir/` entry, as real tars do); every member is
    `read_only` + `Source::kArchiveMember`, which is what makes `-delete` and the exec family REFUSE
    them; `Remove` returns PermissionDenied rather than silent success; `Access` is never writable
    and otherwise follows the stored bits; `ReadLink` resolves a symlink member and refuses a regular
    one; `FsType` is "archive" and `IsCaseSensitive` is true; a path outside the container, a missing
    member and "not a directory" are three DISTINCT errors, not an empty listing; and the configured
    separator is used for rendering AND parsing. `ReadContent` extracts a member's bytes through
    `ReadMemberOfFile` (one streamed pass per read) and keeps its three refusals distinct - not this
    container, no such member, nothing to read (a directory) - because an empty string would make
    `-grep` / `-content` silently match nothing.
  - **Engine mounting SHIPPED: `--archive=roots` and `--archive=all` dive for real.** The walk takes
    a `ContainerMounter` and an `ArchiveDive` (`xff/engine/walk.h`); `run.cc` resolves the mode, hands
    over `archive::OpenContainer` with the `--archive-separator` / `--archive-prefix` spelling, and
    `//xff/cli:xff_full` links `@xff_archive//:archive_register_cc` behind `--//xff:xff_archive`. A
    container is visited as the FILE it is and then descends like a directory, so `-prune`, `-quit`
    and `-maxdepth` apply to members unchanged; `roots` dives only a named root, `all` also dives
    mid-walk (at the position a directory of that name would take, under every `--sort`). Each
    `Visit` now carries the filesystem it came FROM, so a read-predicate on a member reads out of the
    container instead of looking for `a.tar!x` on disk. `ExtraEnabled("archive")` asks the backend
    slot rather than a compile define, so it cannot drift from what is linked. CI now tests with
    `--config=xff_docs` (every extra on), which is the only config where the archive-gated end-to-end
    tests (`XFF_ARCHIVE_ONLY`) exist at all.
  - **Reads all go through the entry's filesystem.** `fields::RenderContext` and the `-hash` /
    `-hasheq` evaluators take the walk's per-entry filesystem, so `{hash}`, `{lines}` and the hash
    actions render a member's own bytes; `content::ContentLineCount` was split out of
    `FileLineCount` so an in-memory member counts lines by the identical binary-sniff rule. The walk
    also carries each entry's LISTED name (`a.tar!one.txt` is named `one.txt`), which a slash-based
    basename got wrong - `-name '*.txt'` matched a member while `-name one.txt` did not.
  - **Nesting SHIPPED: `--archive-depth=N` (default 1), counted in containers.** A nested container
    has no path, so the mounter reads its bytes out of its parent and `ArchiveFileSystem::OpenBytes`
    indexes them from memory (`ReadMember` is the reader's memory twin of `ReadMemberOfFile`); the
    walk carries a `container_depth_` that the cap compares against, and `MemberKeyOf` resolves a
    member against its KNOWN container instead of splitting at the first separator - a nested
    container's own path contains one (`outer.tar!inner.tar`).
  - **`--//xff:xff_all` turns on every extra (2026-08-12).** Each extra is linked when its own flag
    OR `xff_all` is set (`:xff_<flag>_on` in xff/BUILD.bazel, which is what the binaries select on),
    so `--config=xff_docs` - the config the committed XFF.md is generated from, and which must show
    the FULL surface - is one line that cannot fall behind the extras list. It replaced a derived
    XFF_EXTRAS list plus a completeness checker: making the state unrepresentable beats policing it.
  - **Sniff-gating SHIPPED: a NAME gate in front of the reader, `--archive-any` to drop it.** Under
    `all` a file the walk MET is offered to the reader only if its basename carries a container suffix
    (`archive::LooksLikeContainerName`, the reader's formats plus the packages that are one of them
    underneath); a file NAMED on the command line always is, which is why the mounter now receives the
    container's depth (0 = named). `--archive-any` offers everything, for an archive called `blob`.
    Remaining option, deliberately not built: a magic PEEK for the gated case, which needs a
    partial-read VFS operation (`ReadContent` reads the whole file, which is what the gate avoids).
  - **STILL OPEN after the diving slices (audited 2026-08-12, all four verified against the built
    binary, not read off the code):**
    - **Native phar dives now (FIXED 2026-08-12).** `ArchiveFileSystem::Open` / `OpenBytes` try
      libarchive first and the phar reader when libarchive answers InvalidArgument ("not an archive");
      only that status falls through, so a corrupt archive is still an answer rather than a reason to
      guess with another parser. Member reads go back to whichever reader indexed the container, since
      a phar's data offsets come from its own manifest. `plain.phar` and `sha256.phar` now list 3
      members and `-grep` finds the needle; a `.phar.tar` still goes to libarchive, which is pinned so
      the fallback order cannot become an accident. Per-entry deflate/bzip2 members now
      decompress too (raw inflate via zlib with windowBits -15, plus BZ2_bzBuffToBuffDecompress; the
      manifest's uncompressed size IS the output length, so a stream that ends short or long is a
      DataLoss rather than silently truncated content). Remaining phar gap: whole-file-compressed
      `.phar.gz` / `.phar.bz2`, which needs the same decompress-then-parse step as a bare compressed
      single file below.
    - ~~**Native phar never dives from the CLI.**~~ `ArchiveFileSystem::Open` asks libarchive and nothing
      else, so a native `.phar` (and a whole-file-compressed `.phar.gz` / `.phar.bz2`) is "not an
      archive" and the phar reader - which passes its own tests - is unreachable in a real run. Only
      the tar/zip-based variants work: `plain/entrygz/entrybz2/sha256.phar` and both whole-file
      fixtures list 0 members, `tarbased/targz/zipbased` list 6/6/5. FIX: the mount path tries
      libarchive, then the phar reader, then (for a compressed container) decompresses and retries.
      The fixtures already exist, so this is wiring plus one CLI test per variant.
    - **Write actions REFUSE a member now (FIXED 2026-08-12).** `vfs::Metadata` carries the entry's
      `Source` (the listing's `Entry` always did, but the walk hands the evaluator a Metadata), the
      archive filesystem stamps `kArchiveMember` on every node, and `-delete` / `-exec` / `-execdir` /
      `-ok` / `-okdir` report an impossible task through `control.unsupported`: a hard error naming the
      path (exit 2), or a skip under `--skip-unsupported`. A write action on the CONTAINER is untouched -
      the guard keys on the entry, not on "diving is on".
    - **Two opt-in flags rather than refusals (user, 2026-08-12).** The `-exec` half SHIPPED as
      `--archive-extract` (see the READ-ONLY entry below): `{}` renders as the temporary copy, because
      a path the child cannot open is what the refusal was about, and each copy is removed as soon as
      its child finishes. The `-delete` half SHIPPED as `--archive-delete`: the container is
      rewritten from the members that survive, once per container after the walk (the walk is reading
      that same container while it runs), keeping the original's format and compression, written
      beside it and renamed over it only when complete. A NATIVE phar is rewritten by xff's own writer
      (`phar_writer.cc`): the manifest has no absolute offsets, so the surviving entries and their
      stored bytes are copied verbatim, the member count and manifest length are patched, and the
      trailing signature is recomputed (md5 / sha1 / sha256 / sha512). Verified against PHP itself: it
      opens a rewritten fixture and counts the remaining members. Refused with the reason named for a
      format libarchive reads but cannot write; for a TAR-based or ZIP-based phar, whose signature is a
      MEMBER (`.phar/signature.bin`) computed over the rest of the container, so a plain tar / zip
      rewrite would leave it stale and PHP would reject the result; for an OpenSSL-signed phar (no
      private key to re-sign with); for a compressed single file (no member list to rewrite, and a
      whole-file-compressed container would have to be recompressed around the change); and for a
      member of a container nested inside another one. A container left empty is
      kept: an archive with no members is legal, and deleting the FILE was never what `-delete` on a
      member asked for.
    - ~~**`-delete` on a member silently does nothing**~~ (exit 0, no output, no error) and **`-exec`
      hands the child a member path** (`echo a.tar!a.txt`), which no process can open. The design says
      members are read-only and both must REFUSE; the VFS already returns PermissionDenied, so the
      engine is dropping it. Extract-to-temp for `-exec` stays deferred, but the silent no-op cannot.
    - **A bare compressed single file dives now, and so does a whole-file-compressed phar (FIXED
      2026-08-12).** `ReadCompressedSingleFile` uses libarchive's `raw` format on a reader of its own,
      registered ONLY for a file whose name carries a whole-file compression suffix (`.gz`, `.bz2`,
      `.xz`, `.zst`, `.lz4`, ...; tar shorthands like `.tgz` are excluded, being archives already), and
      then confirms a real filter applied - so `liar.gz` holding text stays a text file. The single
      member takes the container's name minus the suffix (`notes.txt.gz` holds `notes.txt`, as
      `gzip -d` would restore) and reports the UNCOMPRESSED size, since the content is decompressed once
      at open. Before presenting one member, the decompressed bytes are offered to `OpenBytes`
      (libarchive, then the phar reader): that is what makes `.phar.gz` / `.phar.bz2` show their real
      members, and a `.tar.gz` keeps going straight to libarchive. All 18 committed fixtures now dive.
    - ~~**A bare compressed single file does not dive.**~~ `one.txt.gz` (a gzip of one file, not a tar)
      lists no members. libarchive's `raw` reader was deliberately left out with `mtree` (both accept
      anything), so the fix is narrow: only when the name carries a compression suffix AND a real
      filter applies, present one member named by stripping the suffix - a text file's filter is
      `none`, so nothing else can be claimed.
    - ~~**Aggregation double counts a dived container.**~~ Shipped as
      `--archive-aggregate=members|container|both` (default `members`), covering `--summary` and
      `--histogram` alike. `members` counts what unpacking would give, `container` what the disk
      holds (identical to the same run with `--archive=none`), `both` keeps the doubling for when it
      is the point. Listing is untouched in every mode: the dual identity means the container is a
      real entry and its members are entries too.
      - The obstacle was WHEN the answer exists: in pre-order the container is visited before it is
        opened, so "was it dived" is not decidable at visit time. Hoisting the open above `VisitOne`
        unconditionally would have cost the `-prune` optimization (`-name '*.tar' -prune` currently
        skips the open entirely) and, at the mid-walk sites, would hold a whole listing block's worth
        of open archives. So the hoist is a walk option (`WalkOptions::mount_before_visit`) that only
        a reduction in `members` mode turns on: it reuses its open on the roots path, and probes then
        reopens mid-walk to keep the peak at one. Everything else keeps today's order and cost.
  - **Container identity is dual:** the archive keeps its real-FS identity (a real `-type f`,
    deletable and actionable) AND parents its members. This falls out of the VFS source tagging -
    container is local-fs, members are archive-member.
  - **Nesting has its own cap `--archive-depth=1`** (decompression-bomb risk), independent of
    `-maxdepth`; members still count toward `-maxdepth` normally.
  - **Detection** is a libarchive content sniff, but in `all` mode it is gated by a known-archive
    extension / magic peek so a whole tree is not sniffed byte-wise. `--archive-any` forces
    sniff-everything (expensive, opt-in). Raw-compressed single files (`.gz` / `.xz` / `.zst` /
    `.bz2`) are one-member archives whose member is the inner name.
  - **The archive VFS is READ-ONLY by default:** `-delete` on a member is a clean error until
    `--archive-delete` arms the container rewrite (above), never a silent no-op.
    The exec family (`-exec` / `-execdir` / `-ok` / `-okdir`) is a clean error too by default, and
    `--archive-extract` is the way past it: the member is written to its own temporary directory
    under its own name and the child is handed that path (`{}` and `{path}` render as the copy,
    -execdir runs in the copy's directory, -ok shows it in the prompt). Each copy goes as soon as its
    child finishes - for a `+` batch or a -j child, when the run ends. Opt-in because the child edits
    a COPY: an in-place tool reports success and changes nothing in the archive. `-delete` stays
    refused whatever the flag says, since removing the copy would be a no-op dressed as a deletion. Encrypted archives get `-encrypted`
    detection only, no `--password` decryption. Read-only member semantics, the `container!member`
    representation, uncompressed logical size and the streaming / bomb limits are specified in
    `docs/design.md` "Virtual entries".
  - **SHIPPED: per-format URI schemes on the PREFIX axis.** `--archive-prefix=URI` now renders the
    spelling the receiving ecosystem parses: a `.phar` as PHP's `phar:///abs/a.phar/inner/x`, a `.jar`
    / `.war` / `.ear` as Java's `jar:file:/abs/a.jar!/pkg/C.class`, everything else as the generic
    `archive:`. By EXTENSION, not sniffed format, because that is what the claim is - a jar IS a zip,
    and only its name says which readers expect it. Both forms fix the separator too, so
    `--archive-separator` does not reach them. Split learned the same two forms, in BOTH overloads:
    the walk maps a rendered path back through the probing one, so teaching only the string overload
    rendered paths the walk could not look up (caught by running the binary, not by the tests).

- **DECIDED (2026-08-13, user): NO `AUTO` separator; per-format spelling belongs to the PREFIX.**
  A member path's value is that you can read it and paste it back, and a per-format separator
  breaks both: phar's own `/` makes `a.phar/inner/x` ambiguous - you cannot tell where the
  container ends without opening it, and a real directory of that shape can exist. One separator
  (`!` by default, `--archive-separator` to change) keeps that property. Per-format schemes are
  right on the axis that is EXPLICITLY an interop artifact: `--archive-prefix=URI` can emit
  `phar:///abs/a.phar/inner/x` and `jar:file:///abs/a.jar!/inner` while the bare path stays
  unambiguous. Task #177 is closed by this; the per-format URI work is its own follow-up.
  - **OPEN (own slice): phar support.** libarchive does NOT read phar (stub + manifest + optional
    per-entry compression + signature), so it needs its own reader behind the same `archive_reader`
    shape - which is what the extras architecture is for, and the member-path spelling is
    format-agnostic, so nothing there changes. Check the existing phar work for a reusable manifest
    parser first. Tracked as task #176.

- **EPIC: container formats beyond phar (raised 2026-08-11).** Survey outcome: almost every "package
  format" is a zip or a tar underneath, so libarchive already reads it and the work is coverage, not
  code. Compiled-in today: `tar, zip, 7zip, cpio, ar, cab, iso9660, lha, rar/rar5, xar, warc, mtree`
  plus the `rpm` filter and every common compressor. Ordered by value per unit of work:
  - **FREE ALREADY, so pin it with fixtures (own slice).** JAR/WAR/EAR, APK/AAB, wheel/egg, nupkg,
    vsix, xpi, docx/odt and Maven / Composer bundles are zip; npm `.tgz`, Cargo `.crate` and OCI
    layers are tar; `.deb` is an `ar`; `.rpm` reads through the rpm FILTER (it exposes the cpio
    payload). `.gem` and `.conda` read one layer deep, the inner `data.tar.gz` needing
    `--archive-depth` > 1. None of this needs a reader - it needs committed fixtures asserting we
    really do read them, which is behaviour we already ship and currently do not test.
  - **PREFIXED PAYLOAD: ANSWERED 2026-08-11, no mechanism needed.** CRX3 (`Cr24` + header + zip), JMOD
    (`JM` + zip), self-extracting installers and by extension AppImage / PyInstaller are all "skip a
    prefix, then hand the rest to a reader we already have" - and libarchive already does it. Its zip
    reader locates the end-of-central-directory by seeking from the end and derives the offset delta,
    so all three committed fixtures read: `sfx-example.zip` (absolute offsets, the real SFX shape) and
    `example.crx` / `example.jmod` (a zip appended verbatim, so every recorded offset is short by the
    header length - measured at 41 and 4 bytes). Pinned by `//:format_fixture_test`; nothing of ours to
    build. phar's stub scan is an instance of the same idea, but moving phar onto a shared mechanism
    stays a LATER question and not while phar works: phar is itself several formats (native
    stub+manifest, tar-based, zip-based), so the refactor is not a one-liner.
  - **ASAR (Electron), BLOCKED on a JSON reader in helly25/mbo.** The one genuinely missing format
    that is both easy and widespread: every Electron app ships `app.asar` (VS Code, Slack, Discord,
    Teams) and that is where the JS lives, so reading inside it is a real want. Shape is phar's
    cousin - a JSON directory tree (offsets / sizes as strings, an `unpacked` flag for files kept
    outside, an integrity block in newer versions) then concatenated payload, no compression.
    REQUIREMENT: a JSON reader in helly25/mbo (we have a writer, no reader). Until mbo grows one,
    this stays unstarted rather than taking a JSON dependency here.
  - **DEFERRED: squashfs.** Snap payloads, AppImage payloads, firmware images. No new codec deps
    (libarchive already links zlib / xz / zstd / lz4), but metadata block tables and fragment
    handling make it a real slice, and a snap is not somewhere people usually grep. Revisit when
    something concretely asks.
  - **OPEN (design sketched 2026-08-11, needs ratification): the PREFIX itself as a listed entry.** A
    prefix is real content - a phar stub is a working PHP bootstrap, an SFX prefix a shell script, an
    AppImage prefix a multi-megabyte ELF runtime, a CRX3 header a signature block - and today the walk
    lists the members and silently drops everything before them, so "find phars whose stub requires X"
    cannot be asked. It wants a synthesized entry, and the naming question has three candidate answers:
    an EMPTY name (rejected: the member path then equals the container path, which is already both a
    file and a directory), REPEATING the container name (rejected: reads as a nested copy and can
    collide with a real member), or an artificial name in a reserved, format-named dot-directory
    (preferred). phar decides it for us: the tar- and zip-based variants already store these as REAL
    members with fixed names (`.phar/stub.php`, `.phar/signature.bin`, `.phar/alias.txt`,
    `.phar/.metadata.bin`), which we read today, so a native phar synthesizing the SAME names makes all
    three variants list identically. By extension `.crx/header.pb`, `.sfx/prefix.bin`,
    `.appimage/runtime`; JMOD needs none (4 bytes of magic, no content). Two consequences to ratify
    with it: VISIBILITY (a dot-directory is already governed by the skip-hidden rules, so find shows
    them and xfd / rg hide them until `--hidden`, rather than adding a flag - the alternative is an
    explicit `--archive-parts=none|meta|all`), and COLLISION (a real member owning the synthesized path
    wins and the synthesized entry is suppressed, never shadowing stored bytes). Note this is the one
    thing that DOES need prefix-offset detection, which reading does not: phar knows its stub length
    already, and for any prefixed zip the length falls out of the end-of-central-directory record.
  - **REJECTED as not worth it:** MSI (OLE2/CFB sector chains, Windows-centric), DMG (UDIF plus
    HFS+/APFS), WIM, Nix NAR (trivial format, tiny audience), py2exe.

- **Third `-regextype` grammar: shell-glob (#121, task-tracked).** Once PCRE2 proves the third-backend
  path, add `Grammar::kGlob` + a `GlobBackend` on the `xff/regex` `RegexBackend` abstraction,
  selectable via `--regextype=GLOB` (and later the find `-regextype` primary). Fits `-regex`/`-iregex`
  as a whole-string shell glob (fnmatch) - a grammar-selected alternative to `-path`. Open nuance:
  glob has no capture groups and no natural match-span, so partial/line matching (`-grep`/`-rxc`) and
  captures / `{field:s/}` rewrite are degenerate - restrict `kGlob` to the whole-match predicates or
  define per-line fnmatch. Cheap on the abstraction; overlaps `-path` for `-regex` (fine - it is about
  letting glob-thinking users pick their grammar uniformly). (Shipped as `--regextype=GLOB`; because it
  compiles to RE2 the partial/span ops are NOT degenerate - `-grep`/`-rxc` work under GLOB.)
- **`--regextype=SHGLOB` - shell glob with brace alternation (#129). SHIPPED.** `Grammar::kShglob` =
  GLOB plus `{a,b,c}` -> RE2 `(?:a|b|c)` (`xff::glob::ShglobToRegex`), so `*.{cc,h}` matches either.
  A separate grammar (not a GLOB feature) because GLOB / gitignore must keep matching literal braces.
  Rules match bash: each alt is itself SHGLOB-translated (nesting, `*`/`?`/`[...]` inside), a comma-less
  `{x}` / unbalanced `{` stays literal, empty alts allowed, `\{`/`\}`/`\,` escape. Deferred: numeric /
  char sequences `{1..9}` / `{a..z}`, and bash extglob pattern-lists `?(..)`/`@(..)`/`!(..)` (the last
  has no clean RE2 form - which is also why the grammar is SHGLOB, not the misleading `EXTGLOB`).
- **Extras architecture v2 - full separation via local modules (#123, DESIGN, revises #311-#317).**
  Post-#317 review (2026-07-10): the shipped approach is not fully separated - the ROOT `MODULE.bazel`
  names `pcre2`, `backend.h` visibility was widened, and a manual `//xff:xff_pcre` flag + a bespoke
  `full` CI cell drive it. Target end-state, so the core has ZERO knowledge of any extra and a
  minimal `xff` source package can ship with the optional parts DELETED (317/5, 317/6):
  - **Layout (317/2) DONE:** renamed `third_party/` -> `extra_modules/` (it holds glue/wrapper code,
    not the vendored lib). Each extra is `extra_modules/<name>/`.
  - **Shared base module `xff_extras_api` SHIPPED (b1 #326, b2 #327):** the RegexBackend plugin
    interface + PCRE2 registration slot (`backend.{h,cc}`) and the license-notice registry
    (`notice.{h,cc}`, `Register`/`Registrar`/`Notices`) live in a standalone top-level local module
    both the core and every extra `bazel_dep`, breaking the cycle (an extra can't dep the core). It is
    at the TOP LEVEL, NOT under `extra_modules/`, so a minimal archive can drop `extra_modules/`
    wholesale. Two targets: `@xff_extras_api//:regex_backend` + `:license_notice`, each keeping its
    logical include path (`xff/regex/backend.h`, `xff/license/notice.h`) via `include_prefix`.
  - **Local module per extra (317/3) SHIPPED for PCRE2 (b3):** `extra_modules/pcre2/` is its OWN local
    Bazel module `xff_pcre2` - its `MODULE.bazel` declares `bazel_dep(pcre2)` + `bazel_dep(xff_extras_api)`;
    root pulls it via `bazel_dep(name="xff_pcre2") + local_path_override(path="extra_modules/pcre2")`.
    The backend deps ONLY `@xff_extras_api` + `@pcre2` (verified: the lean `//xff/cli:xff` cquery has
    zero `extra_modules`/`@pcre2` deps). Disable = comment the root's bazel_dep+override, or delete the
    directory. `extra_modules/` now holds only removable extras.
  - **Auto-enable via a module extension (the "check this"; SPIKE first):** `module_ctx.modules` lists
    only extension PARTICIPANTS, not the whole graph - so each extra must SELF-REGISTER by using the
    extension (from its own MODULE.bazel), and the extension must live in a shared base module both
    root and the extras can load (defining it in root is circular, since root depends on the extras).
    The extension then generates the wiring so `xff_full` links exactly the present+registered extras -
    the piece that makes a root-only patch / dir-removal build `xff_full` lean with no dangling label
    and `@pcre2` never fetched. **Must spike** to confirm this (and "patch root only -> clean strip")
    actually holds in bzlmod before rearchitecting; else fall back to the flag.
  - **Normal build (317/1):** `bazel build //...` builds BOTH lean `xff` and full `xff_full` (extras
    present by default); DROP the separate `full` CI cell. The only separate build is the stripped one
    (the minimal package), which is a patch/removal, not a required cell.
  - **License/NOTICE (317/4):** each extra carries its wrapped lib's own `LICENSE`/`NOTICE` next to its
    `MODULE.bazel` and self-registers its notice (SPDX + copyright, ideally the full text) into
    `xff/license`, as the core deps do - so `xff_full`'s `--help=notice` + generated NOTICE reproduce
    core+extras. The committed root NOTICE stays core-only + a disclaimer that `xff_full` may compile
    in further deps (present-at-load + actively enabled), whose notices then apply. Drift-check: core
    for the committed root NOTICE, full for the extras' set.
  - **Staging:** spike the bzlmod mechanism (local module + self-registration extension + clean strip);
    if viable, implement v2 wholesale (rename + local modules + auto-detect + per-extra notices,
    retiring the `//xff:xff_pcre` flag + `full` cell); #83 archive then follows the same shape.

- **Heavy/special libs are composable build-time extras (decided 2026-07-06).** libarchive (#83),
  pcre2 (#85), and any later special dependency are gated behind Bazel flags, not always compiled
  in: the default binary is a lean core (RE2 only, no archive), and an extended binary is composed
  from the same tree by enabling extras. Per extra: a `bazel_skylib` `bool_flag` (e.g.
  `//xff:xff_archive`, `//xff:xff_pcre`, default False) + a `config_setting` + a `select()` on the FULL
  binary's deps so the extra's backend target (`@libarchive`, `extra_modules/pcre2`) links only when
  on. Presence is then detected at runtime from the registry the backend self-registers into (e.g.
  `regex::Pcre2Available()`), so there is NO `#ifdef` in the core - deleting the extra's directory
  makes an extra-on build fail to compile while the lean build still builds. (The `-DXFF_WITH_*`
  define was #115a's archive interim; PCRE2 supersedes it with self-registration, and #83 will
  follow.) A `.bazelrc` convenience config (`build:xff_full --//xff:xff_pcre`, `--//xff:xff_archive` joins with
  #83) composes them; CI builds both the lean and the full binary. The CLI reports which
  extras are compiled in (`--version` / help) and a disabled feature errors clearly ("not built in;
  rebuild with `--//xff:xff_archive`"), never crashes. This is BUILD-time composition (what code/deps
  are in the binary), distinct from the #73 `--feature` RUNTIME gates. The third-party NOTICE is
  assembled from the enabled extras, so a lean build carries none of their notices.
  - **Scaffolding SHIPPED (#115a):** the `//xff:xff_archive` `bool_flag` + `config_setting`; a structural
    `cli::GlobalFlag.extra` key + `cli::ExtraEnabled(key)` (reads the `XFF_WITH_*` define); the
    `--archive` global, always listed. In a lean build a disabled extra flag stays present but shows
    under a distinct "Extras (not built into this binary)" help group with a `[needs --//xff:xff_archive]`
    note, is documented NOT-built-in by `--help=--archive`, and is a hard immediate error (exit 2)
    **only when used**. Covered by `globals_test` + `extras_test.sh`.
  - **Licenses/notices SHIPPED (#296 interim, then #297 the real design).** Single-file binaries
    must REPRODUCE their notices (pointing at files does not satisfy notice retention). #297 made the
    code the SOT via **self-registration**: `xff/license` holds a `Notice` registry + `Registrar`;
    core deps (Abseil/RE2/mbo) self-register from `license.cc`; `NoticeText()`/`LicenseText()` (the
    latter genrule'd byte-exact from `//:LICENSE`, which stays canonical); `--help=notice` /
    `--help=license` (plural aliases) reproduce the compiled-in set; `license_test` drift-guards the
    committed `NOTICE`/`LICENSE` against the code. No external dep. Author name is `Boerger`.
    **Under self-registration a MINIMAL binary's NOTICE is core-only, which is CORRECT** - the
    libarchive/PCRE2 notices belong to the FULL binary and land with the extras' real modules
    (below). TODO in `license.h`: C++23 `#embed` + reproduce each dep's own license text.
  - **Dual binary SHIPPED (#85 PR4, supersedes the earlier `alias` sketch).** Two real, named
    binaries in `//xff/cli`: `xff` (lean, the target every test/golden runs against and the one built
    by `//...`) and `xff_full` (`tags=["manual"]`, same core + a `select({"//xff:xff_pcre_enabled":
[...]})` on its deps). NO `alias` - an alias's runfile takes the resolved target's basename
    (`xff_minimal`), which would break every bashtest's hardcoded `xff/cli/xff` lookup; two named
    `cc_binary`s keep the `xff` artifact named `xff` (zero test churn), and the user picks which
    binary to run. `manual` keeps the heavy full binary + its deps out of default `//...`.
    `DefaultStyleForProgram` strips a `_full` suffix so `xff_full` -> xff style (and `find_full` ->
    find, etc.); covered by `config_test` + `full_binary_test.sh`. `--config=xff_full` (`.bazelrc`) turns
    the extras on; `--config=xff_full --//xff:xff_pcre=false` drops one from an otherwise-full build.
  - **PCRE2 backend SHIPPED (#85 PR5).** `extra_modules/pcre2/` (removable dir) holds the real
    `Pcre2Backend` (implements `xff/regex`'s `RegexBackend` via the PCRE2 C API - compile / match /
    ovector / substitute), `alwayslink` self-registers via `Pcre2Registrar` + a BSD-3 notice
    (license registry), deps the BCR `pcre2` 10.47 module, and links into `xff_full` via
    `select({"//xff:xff_pcre_enabled": [...]})` - `manual`, so a plain `//...` build never fetches
    `@pcre2`. FullMatch is ANCHORED|ENDANCHORED; ReDoS guarded by match + depth limits; Rewrite
    translates the RE2 `\1` contract to PCRE2 `$1`. Grammar threading (kPcre2) landed in PR5a. Tests:
    `pcre2_backend_test` (unit, all ops, backreferences/lookahead) + `full_binary_test` (config-aware
    end-to-end); a CI `full` cell runs the whole suite + the manual full targets under
    `--config=xff_full`. This completes the RegexBackend engine family: RE2 / EXACT / FNMATCH / GLOB
    (core) + PCRE2 (extra).
  - **REMAINING #83 (archive extra, NOT built):** same shape - `//xff:xff_archive` already exists; add a
    `extra_modules`/libarchive-backed self-registering module linked into `xff_full` via
    `select({"//xff:xff_archive_enabled": [...]})`, join `--//xff:xff_archive` into `.bazelrc build:xff_full`.
    `@libarchive` **3.8.1.bcr.2 RESOLVES** (verified; target `@libarchive//libarchive:libarchive`,
    keep its `use_mbedtls` OFF); codec set tar/gz/bzip2/xz/zstd/lz4, mbedtls deferred; add the
    `-encrypted` detection predicate (no crypto needed).
  - **What CHANGES when the real modules land:** committed `NOTICE` becomes the FULL set (regenerated
    from the full binary); a drift check runs `--config=xff_full` only; CI gains a full cell (builds/tests
    both lean and full). **Decided (was an open detail):** before the real diving lands, a full build
    must NOT silently accept `--archive`. The flag parses and validates its value, then fails with a
    distinct "archive diving is not yet implemented in this build" usage error (exit 2) - deliberately
    different wording from the lean build's "not built in" extras error, so the two states are never
    confused. That guard ships as the first archive slice and is replaced by real behavior later.

- **PCRE2 backend (#85, `-regextype`): SHIPPED as a composable extra - decided 2026-07-06.**
  **Done:** PR3 recognized `--regextype=PCRE2` + guaranteed the "not built in" error; PR4 the
  dual-binary + extras-flag scaffolding; PR5a the grammar threading; PR5b the real `extra_modules/pcre2`
  backend + BSD notice + `xff_full` `select` + CI `full` cell (above). `--regextype` now selects any
  of RE2 / EXACT / FNMATCH / GLOB (core) or PCRE2 (extra). RE2
  (our engine) is linear-time and omits backreferences / lookaround / recursion; pcre2 is the Perl
  superset a `-regextype pcre`/`perl` grammar needs (RE2 already covers the POSIX-family grammars,
  which are all regular). **pcre2 is in the BCR**, upstream-maintained
  (`bazel_dep(name = "pcre2", version = "10.47")` - a stable release, not the 10.46-DEV snapshot); a
  clean dep, BSD-3-Clause (same family as re2 / googletest, so no new license type). Add a
  PCRE2-backed `regex::Matcher` behind the existing `xff/regex` abstraction, gated by the
  `//xff:xff_pcre` extra above; keep **RE2 the default**, PCRE2 opt-in via `-regextype`, and set pcre2
  match / backtrack / depth limits (`pcre2_set_match_limit` etc.) so an adversarial pattern (ReDoS,
  which RE2 is immune to) cannot hang a walk.

- **Richer stats: histograms (#81) - design pinned 2026-07-06.** Histograms of "what the user
  sees": aggregate a metric grouped by a field and draw it as bars. `--summary` (the count+size
  group table) and `--histogram` are **independent, combinable terminal reductions** - a list of
  reduction specs, ONE walk feeds all of them, blocks render in declared order, and any reduction
  suppresses the per-match listing (like `--summary` today; an explicit `-print` / action brings it
  back). `--top=N`, `--summary-precision=N`, and `--human` apply to every block's numeric column.
  - **SHIPPED (all slices).** `--histogram=BUCKET[:MEASURE]`. BUCKET is categorical
    (`overall|type|ext|lang|mime|user|group`, reusing the `--summary` group-by; `owner` is an alias
    of `user`) or a numeric-range field (`size`/`lines` by order of magnitude - "0"/"1-9"/"10-99"/...
    - and `depth` per level, drawn as an ascending distribution). MEASURE is `count` (default) or an
      aggregate `sum/mean/min/max(size|lines)`; a numeric metric with no aggregator is a usage error.
      Unicode block bars via `--unicode` (ASCII `#` fallback), scaled to the tallest; `--top` keeps the
      N tallest (categorical buckets); `--summary-precision` sets `mean`'s decimals; `--histogram-width=N`
      sets the bar cell width (default 40); `--format=jsonl` emits block-tagged rows
      (`{"histogram":...,"bucket":...,"value":...}`); combinable with `--summary`. A `--help=stats` topic
      documents both reductions, pulling its flags from the globals SOT via a `GlobalFlag.topic` tag.
      The `mime`/`user`/`group` categorical buckets reuse the `{mime}`/`{user}`/`{group}` field
      vocabulary (a new `{mime}` field + `{owner}` alias of `{user}`), so bucket keys cannot drift from
      the field values, and `--summary` gained the same three keys. Remaining ideas (time buckets,
      custom edges) are deferred entries below, not part of #81 v1.
  - **Grammar `--histogram='BUCKET[:MEASURE]'`** (repeatable). BUCKET is a `{field}` (categorical:
    `ext` / `type` / `lang` / `mime` / `user` / ...; numeric: `size` / `lines` / `depth`). MEASURE is
    `count` (the default, aggregator-free) or `sum(FIELD)` / `mean(FIELD)` / `min(FIELD)` /
    `max(FIELD)` over a numeric FIELD. **No default aggregator on a numeric metric:** `ext:sum(lines)`
    is valid, bare `ext:lines` (metric without an aggregator) is a usage error naming the four.
    Bucket-first is deliberate - it mirrors `--summary=BUCKET`, matches the bars-are-buckets model,
    and `sum(lines)` reads as the SQL aggregate. Examples: `--histogram=ext` (files per ext),
    `--histogram='ext:sum(lines)'` (total lines per ext), `--histogram='type:mean(size)'`,
    `--histogram=size` (the size distribution, `= size:count`). Shell note: `()` need quoting; a
    bracket-free `ext:lines:sum` stays available as the no-quote fallback.
  - **v1 buckets = categorical + numeric-range.** Categorical -> one bar per value. Numeric -> auto
    ranges (log-scale for `size` / `lines`, per-value or small-linear for `depth`). Time / age buckets
    and custom bucket edges are deferred to the Featured-ideas list below.
  - **Console-adaptive bars, via the existing `--unicode` flag.** Bars reuse the SAME
    `--unicode=auto|always|never` resolver (`engine::ResolveUnicode`) that `--format=tree` uses for
    its box-drawing: Unicode block bars (`█` plus the partials `▏▎▍▌▋▊▉` for sub-cell precision) when
    unicode, plain ASCII (`#`) otherwise - no new style flag. Each row is `label  value  bar`, value
    through the shared number formatter (#86), sorted by value descending. Default bar width ~40 with
    a `--histogram-width=N` override; terminal-width auto-fit (COLUMNS / `winsize` on a tty) is a
    later nicety.
  - **Combined `--format=jsonl` = flat, block-tagged rows, one object per line:**
    `{"histogram":"ext:sum(lines)","bucket":".cpp","value":3120}` /
    `{"summary":"type","group":"file","count":42,"bytes":1048576}`. A nested `{spec, rows}` array is
    rejected - it would be a single JSON blob, breaking jsonl's one-object-per-line / `jq -c` contract.
  - **Metric cost.** `count` / `size` are free from the stat; `lines` is content-derived (reads every
    matched file), so the `lines` metric depends on the first-class `{lines}` field ("Line count as a
    first-class metric" above). The single walk computes each needed field once and feeds all reducers.
  - **Self-doc (part of done):** a `--histogram` `GlobalFlag` entry + a `--help=stats` topic (or fold
    into a `--help=summary`), and the usage page / man / markdown regenerate from those SOTs.

- **Native capture -> line-explode -> group-by reduction (#133).** Fold "run a command per file,
  then group its output lines by an extracted key" into xff (git-blame lines per author is the
  driving case), so the shell `| awk | sort` tail is not needed. The aggregator is a fold over a
  value stream, so cardinality only matters for the measure (count is cardinality-agnostic; a
  per-file numeric measure like size double-counts a per-line key, so v1 is count).
  - **SHIPPED slice 1 (#340):** `{field:m<delim>PAT<delim>REPL<delim>flags}` - the line-oriented,
    list-producing sibling of `s///`. Per line matching PAT, emit the RE2 rewrite REPL; non-matching
    lines dropped. `Template::AsExtraction` returns the value stream; scalar `Render` newline-joins.
  - **SHIPPED slice 2:** `--summary={template}` folds the stream. `{ext}`-style templates group one
    key per matched entry (size meaningful); a single `m//` extraction key groups per extracted line
    (count only, size N/A); a template mixing an extraction with other text is a usage error. e2e:
    `--summary='{capture.blame:m/^author (.+)$/\1/}'` = blame lines per author.
  - **SHIPPED slice 3 (#136):** the agreed (i) - an `m//` extraction in a SCALAR context (any
    `-exec`/`-printf`/`-grep`/... arg, `--template`, or a `--columns` field) is a usage error (exit 2),
    not a silent newline-join. One `FindScalarExtraction` walk over the expression (checking every arg
    is safe - `Template::HasExtraction` trips only on a known field + valid `m//`) plus the `--template`
    / `--columns` strings, refused before the walk. `--summary` is the sole sanctioned list context.
    Friendlier scalar handling is #134.
  - **SHIPPED reducer `;join(...)` (#134):** an m// pipeline may end in a terminal REDUCER that
    collapses the value stream to one scalar, making the SAME extraction valid in a scalar context
    (the explicit opt-in the #136 error asks for). v1 ships `join` in FUNCTION notation: bare `join`
    joins with `\n`, `join(SEP)` a custom separator (with `\t \n \\ \)` escapes), `join()`
    concatenates. Numeric reducers (`sum`/`avg`/`min`/`max`/`count`/`first`/`last`) are reserved for
    the same terminal slot - per-field, so nothing rules out numeric aggregation later. UNIFORM
    pipeline model (decided over an alternative that reserved `;` for the reducer): `;` = "next stage",
    `s///` maps whatever flows (per-line before the reducer, scalar after), so #135's per-line s///
    after m// is kept (incl. in `--summary`) and a post-reducer `s///` rewrites the joined scalar
    (`m/.../\1/;s/ /_/g;join(, );s/_/./g`). The scalar-context guard now rejects only an UNREDUCED
    extraction (`HasUnreducedExtraction`); a reducer in a `--summary` key shifts it from a per-line to
    a per-entry (joined) key, no special-casing. `SplitPipeline` in `xff/fields/fields.cc`. Delimited
    `s///`/`m//` stay as-is (regex args are delimiter-hostile); only reducers use function notation.
- **SHIPPED span-diagram help (#143).** The `--help=fields` topic (and the doc*renderer FIELDS
  section -> `--man` / `--markdown` / `--help=full`) now teach the m// pipeline with a two-line
  ASCII span diagram (ranges under each stage): `|________|` brackets under `m//`, `s//`, `join`,
  `s//` with `extract per line / map each line / reduce stream / rewrite scalar` labels. Rendered
  verbatim via the DocRenderer `Example` primitive (a markdown code fence / roff `.nf`), ASCII-only
  (`| * / ( )`) so the `Roff()`escaper and mandoc keep the alignment; verified in all three
renderers + a help_topic_test assertion. The diagram is duplicated in help.cc`RenderFields` and
  doc_renderer.cc (the pre-existing fields-doc split); unifying those is the deferred #126 work.
  - **SHIPPED chained sed rewrites (#135):** an `s///` or `m//` qualifier takes a `;`-separated command
    chain, applied left to right; a command after `;` may omit the leading `s`. `s` chain = scalar
    substitution pipeline (`{name:s/a/b/;s/c/d/}`); `m` chain = the first command filters+extracts each
    line, the rest substitute on the survivor (`{capture.blame:m/^author (.+)$/\1/;s/ /_/g}` = the
    author, spaces normalized). Shared `ParseRewriteChain` + `CompileChain` in `xff/fields/fields.cc`
    (single command = the one-element case); `;` separates only after the flags, so a `;` inside
    PAT/REPL is safe.
  - **DEFERRED:** `--histogram={template}` (histogram counterpart of the summary key); a numeric
    per-line measure (`{...:m//}` emitting a number + `:sum(...)`), which keeps key and measure at
    the same per-line cardinality.

- **Content-type predicates `-text` / `-binary` / `-eofnl` (#137) - SHIPPED.** Three xff, expensive
  (content-reading) tests, each file-only. `-text` = a regular readable file whose content is text
  (no NUL in the first 8000 bytes - git's `buffer_is_binary` heuristic, also grep/ripgrep's, now a
  single `content::kBinaryNulSniffBytes` used by `-grep`/`-content`/`{lines}`/`-diff`/`-text`/`-binary`);
  `-binary` = the binary complement WITHIN regular files (so `-binary` != `! -text`, which also
  matches non-files); `-eofnl` = ends in a newline (or empty), the newline-termination axis only.
  Compose: `-text -eofnl` = a well-formed text file, `-text ! -eofnl` = the missing-final-newline
  lint. The two blame cookbook recipes now use `-text` (was a silent `-name '*.py'` / `-lang Python`)
  so their titles match, and `git blame` skips binaries. `-text` is deliberately the search heuristic,
  NOT POSIX conformance (POSIX forbids a NUL anywhere + caps line length + requires newline-termination).
  - **`-text[=git|posix|windows|apple]` flavor (#138) - SHIPPED.** A text-definition value on `-text`,
    via `Binding::kText` (attached `-text=VALUE`, like `-hash=ALGO` / `-diff=STYLE`; the flavor lives on
    `Expr::text_flavor`, validated in the parser). Bare `-text` == `=git` = the loose default (no NUL in
    the first 8000, EOL-agnostic; back-compatible). The strict flavors forbid a NUL ANYWHERE and require
    a final terminator (empty is vacuously complete): `=posix` LF-only ending in LF; `=windows` CRLF-only;
    `=apple` CR-only. A no-terminator or mixed-EOL file matches only `git`. `-eofnl` stays the
    flavor-agnostic "ends in LF" primitive (`=posix` subsumes it). One valued predicate, not
    `-posix-text` / a separate `-eol=` axis; unknown flavor is a usage error.
    - **`-eofcr` and `-eofcrlf` final-terminator primitives (#139) - SHIPPED.** `-eofnl` was
      LF-centric ("ends with LF"); `-eofcr` ("ends with a bare CR", the classic-Mac / `-text=apple`
      terminator) and `-eofcrlf` ("ends with CRLF", the Windows / `-text=windows` terminator) complete
      the a-la-carte final-terminator axis, so each line-ending style has a standalone completeness lint
      the way the flavor predicates bundle it. All three share one `EvalEofTerminator(ctx, terminator)`
      body (regular readable file whose content is empty or `absl::EndsWith` the terminator), content-
      class-agnostic on purpose - compose `-text=windows -eofcrlf` / `-text=apple -eofcr`, or negate for
      the missing-terminator lint. A CRLF file ends in LF too, so it satisfies `-eofnl`; `-eofcrlf` is
      the strict form. All three are xff, expensive, `--config=find` rejects them.
      - **Deferred apple/windows subtleties.** The `-text` flavor logic is sound as shipped (a strict
        flavor requires no NUL anywhere + a proper final terminator; a no-terminator or mixed-EOL file
        matches only `git`). BOM handling (UTF-8 BOM is transparent; UTF-16's NULs already fail the
        strict flavors and often `git`) and mixed-ending leniency are left as future refinements if a
        real need appears - not built speculatively.

### Help / docs rendering (post-#126)

The generalized help model (EPIC #154) is **DONE**: one SOT model (`help_model.h`: Document ->
Section -> Content) built by `help_build.cc`, rendered by the plain / markdown / roff backends off a
single `RenderDocument` walk. The imperative `RenderHelp` cluster is retired; `--help` / `--man` /
`--markdown` all render from the model, so they cannot drift. The follow-ons below shipped as part of
it:

- **Structured examples** (SHIPPED): recipes are structured data (`{task, command, note}`); the model
  emits each as a Subsection heading + an Example block + Prose, so every backend renders it natively
  (Markdown fence, roff `.nf`, plain verbatim). `cookbook_test` still guards one run case per recipe.
- **Text-flow width control** (SHIPPED #393-#395 and prior): `WrapText` word-wraps Prose to a target
  width (`--width=N`, else the TTY width / `$COLUMNS`, else unrestricted when piped); Example blocks
  stay verbatim and aligned `{term}` rows keep their layout. Wrapping is **indent-aware** - the budget
  is per-indent-level (width minus the current visible indent), so continuation lines hang under their
  own first line, not the left margin.
- **Color** (SHIPPED #396): the plain backend colors headings (bold), flag/primary names (bold cyan),
  value/item terms (cyan), and verbatim example code (green), gated on `--color=auto|always|never` +
  `NO_COLOR` resolved at the CLI boundary. `WrapText` is ANSI-aware (escapes are zero visible width).
  Color off is byte-identical to before; `--markdown` / `--man` are unaffected.

### Featured ideas (deferred)

Nice-to-haves parked with a design leaning but not yet scheduled; promote to the roadmap above when a
concrete need appears.

- **Time / age-bucketed histograms** (#81): bucket a metric by an `mtime` / `atime` / `ctime` band
  (files-per-week, bytes-per-month, ...). Held out of the #81 v1 (categorical + numeric buckets only)
  because it needs a date-bucketing grammar - bucket size plus boundary / timezone - that overlaps
  `xff/datetime`; design it against that lib.
- **Custom histogram bucket edges / counts** (#81): explicit numeric-range boundaries or a target
  bucket count (e.g. `--histogram-buckets=...`) in place of the automatic log / linear ranging.
  Deferred until the auto ranging proves insufficient in practice.
- **Pager for long help / reference output** (SHIPPED #397): `--pager[=auto|always|never]` mirrors
  `--color`'s tri-state (bare == always, `--no-pager` == never, default auto = page only on a tty); it
  pages the long meta surfaces (`--help`, `--help=TOPIC`, `--man`, `--markdown`) and never the file
  listing. The command is `$XFF_PAGER` -> `$PAGER` -> built-in `less -FRX` (`-F` so short help never
  traps, `-R` keeps the color, `-X` keeps short output on the normal screen); an empty env value
  disables. Paging runs via `sh -c` (args / pipelines work), with a stdout fallback on any failure.
  Rejected: a help-scoped `--help-pager` name and a `--help=paged` content topic - paging is an
  orthogonal behavior, not a content selector.
  - **The FILE LISTING can be paged too, SHIPPED as `--pager=all` (asked 2026-08-13).** `auto` stays
    meta-only; `all` adds the listing, and is the one value that touches ordinary output. It is
    STREAMED rather than buffered: the pager is started once and this process's stdout is redirected
    into it for the whole walk, so the first screen appears while the walk is still running and every
    writer (the renderers, a child process) is paged without knowing about it. Three deliberate
    edges: `all` is terminal-only (there is no "always page the listing" - through a pipe the pager's
    screen handling would become the next command's input); it steps aside for an expression that
    needs the terminal itself, read from the registry (`Descriptor::terminal` on `-exec` /
    `-execdir` / `-ok` / `-okdir`) rather than a name list in the CLI, and for `--quiet`; and
    quitting the pager early ends the run quietly (SIGPIPE ignored, the failing writes swallowed).
    The spelling was the open part - `--pager=all` reads as "page all of it" and keeps one flag with
    one axis, against a second `--pager-scope=meta|all` flag that would have split the axes at the
    cost of a knob.
- **CREATING archives (raised by the user 2026-08-14; task #193): "another killer feature if done
  right."** xff already walks, matches and (with `-Z`) rewrites containers; packing the matched set
  into a NEW archive is the missing direction, and it composes with the whole expression vocabulary -
  `xff src -name '*.cc' -newer x` becomes a tar/zip of exactly that set, with no intermediate file
  list and no shell plumbing. Open questions before building: the spelling (an ACTION like `-pack
FILE`, which reads per-match, versus a reduction like `--summary`, which is what a single shared
  output really is); format from the output NAME with an explicit override; member naming (relative
  to the root, the cwd, or a stripped prefix - a wrong default bakes absolute paths into archives);
  the read/write interaction (`-Z++ -z-` is precisely "pack without harvesting from existing
  containers", while `-z+ -Z...` is the deliberate repack); refusing an output path that lies inside
  the walk (it would feed itself); and reproducibility (deterministic `--sort` order plus
  mtime/uid/gid normalisation). Start with tar (+ compression) and zip.
  - **Answered, and SHIPPED as the first slice:** a reduction, not an action (one archive per run is a
    sink like `--summary`, so it replaces the listing while explicit actions still run); format from
    the output name, checked before the walk; member names relative to the search root the entry came
    from; the output file skipped when the walk meets it, the way `tar` skips the archive it writes;
    an archive MEMBER refused outright, since harvesting from one container to pack into another is
    the separate feature `-Z++ -z-` reserves; times preserved, ownership pinned at 0:0 for
    reproducibility; and one knob, `--pack-level=N`, validated against each format's own range.
  - **`--pack-option=NAME=VALUE` (repeatable), with an XFF-OWNED vocabulary - SHIPPED.** Settled with
    the user 2026-08-15 and built the same day: `NAME` is xff's and the backend TRANSLATES it, so an
    unknown name is a usage error, a name that does not apply to the chosen format is refused naming
    the ones it does, and `--help=archive` renders the table from the LINKED writer rather than a
    copy. First vocabulary: `compression` (zip store/deflate), `level`, `threads` (xz/zstd),
    `timestamp` (gzip header, `no` makes two runs byte-identical), `zip64`. `--pack-level` stays as
    the shortcut for the one knob every compressed format has, the relationship `-Z` has to
    `--archive-write`. A raw passthrough of libarchive's own option names was rejected: they change
    between its versions and could be neither validated nor generated into the help.
  - **Still open: a `@file.json` form** (`--pack-option=@opts.json`), which is the same vocabulary
    read from elsewhere. Cheap now that the table exists; wants a JSON reader (see the ASAR entry,
    blocked on one in helly25/mbo) rather than a hand-rolled parser.

- **Fuzzy finding + near-duplicate detection** (design open). Two distinct capabilities that share the
  "approximate match" theme; split them, do not conflate:
  1. **Fuzzy name matching - v1 SHIPPED as `-fuzzy` / `-ifuzzy`** (subsequence over the BASENAME,
     kXff-gated, `//xff/fuzzy`). The algorithm question is answered for this half: a SUBSEQUENCE
     match (fzf / quick-open: the characters of PATTERN appear in order, gaps free), because that is
     the question a file finder asks - "can I type a few letters and find the file" - while bounded
     edit distance answers a spell-checker's "is this a typo of that" and would miss `tmh` ->
     `the_main_header.h` entirely. Deliberately NOT built yet, each because it needs a decision
     rather than more code:
     - **Ranking - the SCORE half is SHIPPED (2026-08-15).** `fuzzy::Score` does the alignment search
       the greedy scan could not (the earliest match for each character is always A match and often
       not the BEST one), rewarding word starts, consecutive runs and early matches; `{fuzzy}` renders
       it, so `--columns=fuzzy,path` plus a numeric sort is already a ranking. The score is stored per
       entry by the evaluator and cleared before each one, so it cannot leak between entries.
       - **Still open: making the ORDER xff's own** (`--sort=score`, probably `--top=N`). That is a
         different change from scoring: every other `--sort` mode is a traversal order the walk can
         stream, while ranking has to buffer the whole result set before it can emit anything. Decide
         what `--sort=score` does when no `-fuzzy` ran (leaning: a usage error, since sorting by a
         value nothing produced is a mistake, not an empty ordering) and whether `--top=N` is
         fuzzy-specific or a general "first N after sorting".
     - **A path-matching variant** (`-fuzzypath`?), the `-path` to this `-name`. The basename is the
       fzf-ish default; matching whole paths without ranking tends to match nearly everything.
     - **A score threshold** as the gate for the boolean form. Now possible (there is a score), still
       unspelled: the value is only comparable within one pattern, so an absolute `-fuzzy=N PATTERN`
       cut-off would be a number users cannot reason about. A relative gate (a fraction of the best
       score in the run) needs the whole result set, so it belongs with `--sort=score` rather than
       before it.
  2. **Content near-duplicate / similarity** via **w-shingling**
     (https://en.wikipedia.org/wiki/W-shingling): represent each text file as the set of its
     contiguous w-token shingles (w-word or w-character k-grams), and score similarity as the Jaccard
     overlap of two shingle sets; **MinHash** approximates Jaccard cheaply so it scales to a whole
     tree without O(n^2) full-set comparisons. Use cases: "find files similar to X" (a per-entry
     matcher against a reference file via the field vocabulary, like `-cmp`/`-diff` take a target),
     and grouping near-duplicates across the walk (a reduction, like `--summary`, emitting clusters).
     Design against the existing content/text machinery (`-text` gating, the content readers) and the
     hashing lib (MinHash wants a fast hash; reuse xff/hash or mbo::digest). Open: shingle width w and
     the similarity threshold as flags; whether v1 is the pairwise matcher only, deferring the
     cross-tree clustering reduction. Likely a build-time extra if it pulls weight.
- **Untested `cc_library` targets + a lint to keep them from reappearing (opened 2026-08-10; RESOLVED
  2026-08-10).** STYLE_CPP says all exported code is tested at every level, but nothing enforced it,
  so gaps accumulated quietly. Audited every `cc_library` for a `cc_test` in the same package
  depending on it: **4 of 50 had none**, and 3 of those are in the shared extras API - the worst
  possible place, since it is the module other modules implement.
  - `xff_extras_api:regex_backend_cc` - FIXED: `backend_test` implements the seam with a literal
    backend built from nothing but that module, and pins the registration slot (unregistered reports
    `Unimplemented`, never a bad-pattern `InvalidArgument` and never a silent RE2 fallback).
  - `xff_extras_api:license_notice_cc` - FIXED: `notice_test` pins the documented contract
    (`Notices()` sorted by component regardless of static-init order, registrars contributing the
    whole notice, `Notices()` returning a copy). Before that the only coverage was indirect: the
    archive reader's test asserting its own notice appears.
  - `xff_extras_api:vfs_cc` - FIXED: it now has a contract test that implements the interface with a
    fake backend built from nothing but that module.
  - `xff/cli:main_cc` - not a gap: it is `main()` plus argv wiring, covered end to end by the
    `xff` / `xff_full` bashtests. It is now the lint's single allowlist entry, with that reason
    recorded in the tool, so it reads as a justified exception rather than an oversight.
  - **Enforce it, do not just fix it:** SHIPPED. `tools/check_cc_library_tested.py` + the
    `check-cc-library-tested` hook fail on a `cc_library` that no `*_test` rule in its own package
    depends on. Transitive coverage and non-test dependents (a `cc_binary`, a `bashtest`) do not
    count, since neither exercises the unit directly. It shares one tested BUILD reader
    (`tools/build_rules.py`) with the `_cc` naming lint so the two cannot disagree about what a
    rule is. Both are BUILD hygiene a reviewer should not have to remember.
  - Known limits of the textual reader, deliberate: a target built by a macro or a comprehension is
    invisible, and `deps` reached through a variable is not resolved. A finding is therefore "prove
    it or allowlist it"; `bazel query` is the authority if the two ever disagree. The reader is
    cross-checked against it today - both see exactly 50 `cc_library` targets.
- **The MSan finding: DIAGNOSED as a false positive, then FIXED AT THE ROOT - the toolchain swaps in
  the instrumented libc++ itself (opened 2026-08-10; diagnosed 2026-08-10; RESOLVED 2026-08-11).**
  - **The fix, and why the earlier "structurally blocked" verdict was wrong.** `toolchains_llvm`
    **1.8.0 - the version xff already depended on** - ships first-class MemorySanitizer support, so
    none of the hand-rolled machinery below was ever needed.
  - **The overlay:** `llvm.toolchain(libcxx_url = ..., libcxx_sha256 = ...)` unpacks a prebuilt
    **instrumented libc++** into `libcxx-msan/` inside the LLVM distribution repo, and its headers
    and archives join the toolchain's own filegroups, so they are declared compile and link inputs.
  - **The switch:** `--features=msan` (Linux only) is the toolchain's MSan feature. It turns on the
    sanitizer **and** moves the whole standard library over to that overlay in one step:

    ```
    -fsanitize=memory -fsanitize-memory-track-origins -fsanitize-link-c++-runtime
    -cxx-isystem .../libcxx-msan/include/c++/v1   -L .../libcxx-msan/lib
    -stdlib=libc++ -rtlib=compiler-rt -l:libc++.a -l:libc++abi.a -l:libunwind.a
    ```

    Critically, the ORDINARY stdlib flags live in a mutually exclusive `..._nomsan_stdlib` feature
    that is gated off when `msan` is on.

  - **That gating is the whole difference.** Our attempt ADDED the instrumented tree (as `-isystem`,
    then as a `cc_library` dep) while the toolchain's own libc++ stayed on the search path, which is
    what produced the duplicate-`using` / `#include_next` errors recorded below. The conclusion drawn
    from that - "a hand-built stdlib cannot be swapped in under `toolchains_llvm` at all" - was right
    about the METHOD and wrong about the toolchain: swapping is exactly what `--features=msan` does,
    and it is what upstream's own end-to-end `msan_test` CI job exercises.
  - **A `--features` flag is also the correct shape for a sanitizer:** bazel resets `--features` to
    `--host_features` in the exec configuration, so build tools stay uninstrumented for free.
  - **Where the instrumented libc++ comes from.** LLVM publishes no instrumented libc++, so we point
    `libcxx_url` at the same third-party build `toolchains_llvm`'s own MSan test uses
    (`RealtimeRoboticsGroup/toolchains`, 3.9 MB, `include/c++/v1` + `lib/libc++*.a` - the exact layout
    `libcxx-msan/` expects). Nothing is built locally: `tools/build_msan_libcxx.sh` (cmake + ninja +
    a full LLVM source download) and the `@msan_libcxx` repository are DELETED, and the CI cell lost
    its cmake/ninja install, its libc++ build step and its second cache mount - it is now a plain
    `bazel test`, the same shape as the `tsan` cell.
  - **Kept:** the `xff/cc.bzl` wrappers (single load site, the `msan` tag, the suppression file in
    runfiles), the `no-raw-rules-cc-load` / `check-cc-target-naming` / `check-cc-library-tested`
    hooks, and `MSAN_SYMBOLIZER_PATH` in the run-under wrapper. The wrappers no longer inject a
    libc++ dep - that was the part that could not work, since a dep's include path is additive.
  - **Not needed: a switch to `hermeticbuild/hermetic-llvm`.** The parked note below named it as the
    only real fix. It is not required, because the capability it was wanted for (runtimes built under
    a sanitizer config) is available in the toolchain we already use.

  The original diagnosis, kept because it is what identified the cause. The symbolized frames (once
  `MSAN_SYMBOLIZER_PATH` was set) showed every test failing at the same single site, and identified
  it as an instrumentation gap rather than a bug:

  ```
  #0 std::__1::basic_string<...>::__is_long()  external/toolchains_llvm.../include/c++/v1/string:2142
  #1 std::__1::basic_string<...>::size()       external/toolchains_llvm.../include/c++/v1/string:1290
  #2 std::__1::operator==<char, ...>(...)      external/toolchains_llvm.../include/c++/v1/string:3564
  #3 absl::flags_internal::FlagRegistry::RegisterFlag(...)  absl/flags/reflection.cc:119
  #6 __cxx_global_var_init                                  absl/flags/parse.cc:111
  Uninitialized value was created by an allocation of 'ref.tmp' in RegisterFlag
  ```

  The `std::string` frames resolve to `external/toolchains_llvm.../include/c++/v1/string`, the
  PREBUILT (uninstrumented) libc++, **not** to the instrumented `.msan-libcxx` copy `--config=msan`
  is meant to swap in. So the `ref.tmp` temporary is built by code MSan never saw, no shadow is
  written, and the `==` in `RegisterFlag` reads it as uninitialized. It fires at static init in
  `absl/flags/parse.cc`, which is why literally every test hits it. Nothing in xff or absl is wrong.
  - **Shipped:** `tools/msan_suppressions.txt`, a RUNTIME suppression file. MSan reads it at process
    start (`MSAN_OPTIONS=suppressions=`), so it travels as a declared **runfile**: the `cc_binary` /
    `cc_test` wrappers in `xff/cc.bzl` add `//tools:msan_suppressions` to `data` under
    `--config=msan` (gated by `--//xff:xff_msan`), and `MSAN_OPTIONS` names it runfiles-relative,
    since a test runs with its runfiles tree as the working directory.
  - **Why not a `--copt=-fsanitize-ignorelist=`:** a bare copt is not a declared input. Bazel then
    does not know the file exists, so editing it invalidates nothing; `%workspace%` is not expanded
    inside a copt VALUE (a literal `%workspace%/...` reached clang and every compile failed); an
    absolute include-ish path is rejected as "outside of the execution root"; and it only works by
    reading a file the action never declared, which a stricter sandbox or remote execution refuses.
  - **Coverage gap, deliberate - and now much smaller:** the three `xff_extras_api` tests do NOT route
    through the wrappers, so they carry no suppression file. That module is built both as
    `//xff_extras_api:...` (it is not in `.bazelignore`) and as its own `@xff_extras_api`, and in the
    latter a `//xff:cc.bzl` label does not exist - loading it would also invert the "an extra never
    depends on the core" rule. The `no-raw-rules-cc-load` hook exempts that directory for the same
    reason. What they miss is now only the (empty) suppression file: the INSTRUMENTED LIBC++ reaches
    them anyway, because it comes from the toolchain, which every module in the build shares. That is
    a second, quieter win of moving the swap into the toolchain - a per-target `deps` swap could never
    have covered a module that is forbidden from naming a core label.
  - **CORRECTION (2026-08-12): MSAN ITSELF NEVER READS IT, and the plumbing stays anyway.** MSan has no
    runtime suppression support: `suppressions=` is a sanitizer_common flag MSan parses and never
    consumes (compiler-rt's msan sources contain no suppression machinery - only the tools that build a
    SuppressionContext act on it), so this file has never been opened. Deleting it was proposed and
    REJECTED: TSan, LSan and UBSan do consume `suppressions=`, so the delivery mechanism is wanted
    regardless, and rebuilding it under pressure would be worse. What was actually missing is a test,
    now `//xff:suppressions_delivery_test`: for every `*_OPTIONS` variable naming a suppressions file,
    the file must resolve from the test's working directory (its runfiles tree). It skips in an ordinary
    build and bites in the sanitizer cells.
  - **Known gap the test documents:** an extras-module test cannot load xff/cc.bzl, so it carries no
    suppressions file. Harmless while only MSan (which ignores the option) names one; FATAL the moment
    TSan does, because a missing file makes compiler-rt print "failed to read suppressions file" and
    Die(). Fix that when wiring TSan suppressions - the file wants to live somewhere both the core and
    the extras can depend on.
  - **Format caveat (now moot):** MSan's runtime suppressions understand only `interceptor_via_fun` /
    `interceptor_via_lib` (reports raised through an intercepted libc call), and this report came from
    inline `std::string` code - so the entry may never have matched. With the real libc++ instrumented
    the report is gone at its source, and `tools/msan_suppressions.txt` is empty of real entries; the
    plumbing that carries it stays, so a genuine future false positive has a home.
  - **The dead end, recorded so it is not retried.** Every attempt to bolt the instrumented tree on
    from OUTSIDE the toolchain failed, each in its own way: `%workspace%` is not expanded in a copt
    value (so it silently did nothing), an absolute `-isystem` is rejected as outside the execroot,
    a `-cxx-isystem` breaks libc++'s C-header shims (they reach glibc via `#include_next`), and a
    `cc_library` dep is additive, so the toolchain's own libc++ stayed alongside ours:

    ```
    .msan-libcxx/include/c++/v1/__functional/hash.h:40:8: error: reference to unresolved using declaration
       std::memcpy(std::addressof(__r), __p, sizeof(__r));
    .msan-libcxx/include/c++/v1/cstring:82:1: note: using declaration annotated with 'using_if_exists' here
    .msan-libcxx/include/c++/v1/cwchar:136:9: error: target of using declaration conflicts with declaration already in scope
    ```

    The lesson generalizes past MSan: **the standard library is the toolchain's to choose.** Anything
    that changes it belongs in the `cc_toolchain` (a feature that swaps both the include and the link
    flags, and turns the old ones off), never in per-target flags or deps.

- **RESOLVED (2026-08-10): the `clang-tidy` CI cell was ~5x slower than helly25/mbo's.** Cause found
  and fixed: `actions/cache@v4` only SAVES on a key MISS, and the key changed only with
  `MODULE.bazel.lock` / `.bazelversion` - so after the very first run every run was a cache hit that
  never updated, freezing the disk cache and recompiling all of xff each time. Fixed by splitting
  restore/save (`actions/cache/restore` + `actions/cache/save`) with a rolling `github.sha` key, saving
  only on `main` so branches read main's warm cache without each PR writing its own, plus
  `--experimental_disk_cache_gc_max_size=4G` so a rolling cache cannot eat the 10 GB repo budget and
  evict other jobs' entries. Confirm the win by comparing the next few main runs against the ~34 min
  baseline below.
- **Original investigation notes (kept for the measurements):**
  Measured on 2026-08-10: xff ~34 min per run on main (07:07:17 -> 07:41:34, and 00:03 -> 00:37),
  while mbo's equivalent job is ~6 min warm (09:18:29 -> 09:24:19; 22:57 -> 23:04) with one ~36 min
  outlier that looks like a cold cache. So mbo's cold cost matches ours and its WARM cost does not,
  which points at cache reuse rather than at clang-tidy itself. Two concrete differences found by
  diffing the two workflows - both plausible, neither yet proven:
  1. **Our cache is written once and then frozen.** We use the combined `actions/cache@v4` with a
     STABLE key (`bazel-disk-clang-tidy-<hash of MODULE.bazel.lock + .bazelversion>`). The combined
     action only SAVES when the key missed, so after the first run the key always hits and the entry
     is never refreshed - it stays at the first run's contents while the tree keeps growing. mbo
     instead uses `actions/cache/restore@v5` with a per-commit key
     (`clang-tidy-<ref>-<sha>`) plus a restore-keys ladder (`clang-tidy-<ref>`,
     `clang-tidy-refs/heads/main`, `clang-tidy`) and an explicit
     `actions/cache/save@v5` guarded by `cache-hit != 'true'`, so nearly every run stores a fully
     warm cache for the next one.
  2. **We cache far less.** We cache only `~/.cache/bazel-disk` (the disk cache); mbo caches the
     whole `~/.cache/bazel` bazel root, so it also reuses the output base and skips analysis. That
     matters here because our job runs `compile_commands-update.sh`, which does a full
     `bazel build --config=clang-tidy //...` - the expensive part, and the part a warm output base
     would mostly skip.
     **Tension to resolve, not ignore:** the stable key was a deliberate earlier fix - a per-run key
     minted a fresh cache every commit and the entries were evicted before reuse under GitHub's
     10 GB/repo limit. And caching the whole bazel root would pull in the extracted ~4.4 GB hermetic
     LLVM, which we deliberately do NOT cache for exactly that reason. So the fix is a size-aware
     version of mbo's shape (e.g. per-ref rather than per-SHA keys, an explicit save, and excluding the
     LLVM external dir), measured against the cache budget - not a copy-paste of mbo's job.
- **Evaluate MemorySanitizer (MSan)** as a fourth sanitizer alongside asan / tsan / (ubsan). MSan
  catches reads of uninitialized memory, which asan does not. Feasibility check, not a commitment:
  - **macOS: out.** MSan is Clang-only and effectively Linux/x86-64 only; there is no macOS support, so
    at most a Linux CI cell (`ubuntu`), never the macOS one.
  - **The blocker is an instrumented libc++.** MSan reports false positives on any code it did not
    instrument, so the C++ standard library must itself be built with `-fsanitize=memory`. Everything
    else we build from source under bazel (abseil, re2, pcre2, mbo), so a `--config=msan` propagates the
    flag to them for free; the standard library is the hard part. Check whether the hermetic LLVM
    toolchain can supply (or be made to build) an MSan-instrumented libc++ / libc++abi, or whether that
    is prohibitively heavy in CI.
  - **DONE 2026-08-11: the toolchain supplies it.** The answer to the question above is the first
    option: `toolchains_llvm` >= 1.8.0 CAN supply an instrumented libc++, through the `libcxx_url`
    overlay plus its `msan` cc_feature (`--features=msan`). Nothing is built in CI, so the cost is a
    3.9 MB download rather than a cached cmake/ninja LLVM-runtimes build. See the resolved entry
    above for the mechanism, and for the dead end that came first.
  - **CI:** one `msan` cell (ubuntu only) mirroring `tsan` - a plain `bazel test`, hard-gated in
    `done`'s `needs` like every other cell.
