# xff

eXtended File Find, a find(1)-compatible file finder with modern extensions.

**Usage:** `xff [option...] [path...] [expression]`

## Description

xff walks each starting path and acts on the entries matching an expression, like `find`(1). With no path it searches the current directory; with no action it prints each match.

xff has two flavors selected by the program name: invoked as `find` it is strict find (only the standard vocabulary); invoked as `xff` it enables the modern extensions. An explicit `--config=find|xff` overrides the program name. Items marked as xff extensions below are the additions over find.

## Options

### Config
- `--config=NAME` - select a config style: find (strict), xff (evolved), rg (opinionated); repeatable _(xff)_
  A config style sets the defaults for ignore files, hidden files, sizes, sort order, and case. find is strict find compatibility; xff keeps find's grammar but sorts and prints human sizes; rg is opinionated (respect .gitignore, skip hidden, smart case). Repeatable and layered, last one wins. See --help=styles for the per-style defaults.
- `--no-config` - ignore discovered .xffrc files _(xff)_
- `--xffrc=FILE` - also load a specific config file (a non-arming tier; see --allow-exec) _(xff)_
  Loads FILE as a config tier above the user config (naming it is consent to LOAD it). It is a NON-ARMING tier: safe directives apply, but a dangerous one - the exec family (-exec/-execdir/-ok, -capture) or -delete - is inert unless --allow-exec is set from a trusted tier (the CLI or the user/system config, never from an --xffrc file itself). An unarmed dangerous line is dropped with a one-line warning. Repeatable; later files win.
- `--allow-exec` - arm dangerous directives loaded from an --xffrc file (exec family, -delete) _(xff)_
  Permits the sensitive/destructive directives (the exec family -exec/-execdir/-ok and -capture, and the destructive -delete) carried by an --xffrc-loaded file to actually run. Honored only from a trusted tier - typed on the CLI, or set in the user/system config - never from an --xffrc file (so a named config cannot authorize itself). The root-owned system [policy] can hard-deny even this. Without it, such lines are inert (dropped + warned); -delete still obeys its own --safe/--dry-run guards.
- `--explain` - print the resolved configuration and exit _(xff)_

### Traversal
- `-H` - follow symlinks named on the command line, not while walking
- `-L` - follow symlinks everywhere during the walk
- `-P` - never follow symlinks (the default)
- `--archive` - descend into archives (tar/zip/...) as virtual paths _(xff)_
  Treats each archive (tar, gz, bzip2, xz, zstd, lz4, zip, ...) as a directory, so the whole expression - including -grep on entry content - matches its entries at virtual paths like `foo.tar.gz/inner/x`. Read-only. A build-time extra: the stock binary is lean and omits it (rebuild with --//xff:archive); using --archive without it is a hard error.
- `-j N, --jobs=N|all` - worker count for the walk and concurrent -exec (all = every core) _(xff)_
- `--sort[=none|dir|subtree|tree]` - sibling/traversal ordering (default depends on the mode) _(xff)_
  none leaves entries in filesystem order (fastest); dir sorts each directory's entries; subtree and tree give a deterministic order across the whole walk. The default is per style: xff sorts per directory, while find and rg leave the order unspecified.

### Matching
- `--block-size=SIZE` - bytes per -size block for a bare -size N / -size Nb (default 512) _(xff)_
- `--exact` - match -name/-path byte-exact, opting out of the xff FS-native case default _(xff)_
- `--case=sensitive|insensitive|smart, -i, -s[+|-]` - letter case for matchers: -i insensitive, -s/-s+ smart, -s- sensitive (rg -> smart) _(xff)_
  Controls case for -name/-path/-regex and the content matchers. sensitive matches exactly; insensitive (-i) folds case; smart (-s / -s+) folds only when the pattern is all lower case and matches exactly otherwise; -s- forces sensitive. rg defaults to smart.
- `--regextype=RE2|EXACT|FNMATCH|GLOB|SHGLOB|PCRE2` - match engine: RE2, EXACT, FNMATCH, GLOB, SHGLOB (GLOB + {a,b}), or PCRE2 (a build extra) _(xff)_
  Selects the grammar for -regex/-iregex and the content matchers -rxc/-grep. RE2 (the default) is linear-time regular expressions; EXACT is a literal string (metacharacters are plain text); FNMATCH is a flat shell wildcard where * matches any character including /; GLOB is a path-aware shell glob where */? stop at / and ** crosses directories (gitignore semantics), with [...] classes; SHGLOB is GLOB plus {a,b} brace alternation, so *.{cc,h} matches either. PCRE2 (Perl syntax: lookaround, backreferences) is the one build-time extra: it is present only in a full build, and selecting it in a lean build is a hard error, never a silent fall back to RE2. RE2/EXACT/FNMATCH/GLOB/SHGLOB are always built in; run xff --help=extras to see whether THIS binary includes PCRE2. See --help=grammars for a full description of each grammar (GLOB/SHGLOB are xff's own, not POSIX glob(7)).

### Filter & Ignore
- `--exclude=GLOB` - skip paths matching a gitignore-style glob (repeatable; a matched directory is pruned) _(xff)_
- `--include=GLOB` - re-include paths a --exclude would skip, matching a gitignore-style glob (repeatable) _(xff)_
- `--gitignore[=on|off], -g[+|-]` - respect .gitignore files: -g = auto (only in a git repo), -g+/=on always, -g-/=off never _(xff)_
  Reads .gitignore rules while walking, including nested .gitignore files, .git/info/exclude, and core.excludesFile. -g / auto activates only inside a git working tree; -g+ / =on forces it anywhere; -g- / =off disables it. Independent of --ignore-files (.ignore / .xffignore).
- `--ignore-files` - respect per-directory .ignore and .xffignore files (off by default) _(xff)_
- `--ignore-file=PATH` - read an extra gitignore-format file, rooted at its own directory (repeatable) _(xff)_
- `--no-ignore, -u` - disable all ignore-file processing (.gitignore/.ignore/.xffignore) _(xff)_
- `--ignore-vcs` - respect version-control ignore files (.gitignore / .git/info/exclude / core.excludesFile) _(xff)_
  The rg-style affirmative for the VCS ignore-file layer - today git's (.gitignore at any depth, .git/info/exclude, core.excludesFile), the same layer -g / --gitignore auto enables. Use it to countermand an earlier --no-ignore-vcs or a style default. Independent of --ignore-files (.ignore / .xffignore), which keep their own switch; --no-ignore / -u still turns off every ignore source. Last of the ignore-mode flags wins.
- `--no-ignore-vcs` - do not respect version-control ignore files (keeps .ignore / .xffignore) _(xff)_
  Drops the VCS ignore-file layer (git's .gitignore / .git/info/exclude / core.excludesFile) while leaving --ignore-files (.ignore / .xffignore) untouched - that is the difference from --no-ignore / -u, which turns off every ignore source. Today git is the only VCS ignore file xff reads, so this is nearly --gitignore=off. Last of the ignore-mode flags wins.
- `--hidden` - include hidden dotfiles in the walk (default: find/xff show, rg skips) _(xff)_
- `--no-hidden` - skip hidden dotfiles (the rg default; opts find/xff out) _(xff)_
- `--skip-vcs[=git,hg,svn,jj,bzr,darcs,cvs|all|none]` - prune VCS metadata dirs (.git, .hg, ...); bare/=all = every known VCS, =LIST a subset _(xff)_
  Prunes version-control metadata directories at any depth (like ripgrep / fd), so a search never wades into repo plumbing. Bare --skip-vcs (or =all) covers every known VCS: git (.git), hg (.hg), svn (.svn), jj (.jj), bzr (.bzr), darcs (_darcs), cvs (CVS). A comma list (--skip-vcs=git,hg) is an explicit, frozen subset - it never changes if a VCS is added to the default set later. --no-skip-vcs (or =none) turns it off. Independent of --hidden, so the user's own dotfiles (.bazelrc, .gitignore) still show. -g / gitignore mode implies --skip-vcs=git (only .git); an explicit --skip-vcs overrides that. Default off otherwise.
- `--no-skip-vcs` - keep VCS metadata dirs in the walk (opts out of --skip-vcs and the -g .git default) _(xff)_

### Output
- `--format=plain|nul|jsonl|csv|tsv|aligned|markdown|tree` - output format: plain, nul, jsonl, csv, tsv, aligned, markdown (md), tree; default plain _(xff)_
- `--no-header` - omit the header row from tabular --format (csv/tsv/aligned/markdown; on by default) _(xff)_
- `--columns=FIELD,...` - columns for tabular --format, from the {field} vocabulary (e.g. path,size,mtime) _(xff)_
- `--diff-algorithm=naive|direct|myers` - diff engine for -diff: naive, direct, or myers (the default, minimal like git) _(xff)_
- `--diff-ignore=TOKEN,...` - normalize -diff comparison: ws, change, trail, blank, case, eofnl (comma-separated) _(xff)_
- `--diff-ignore-matching=REGEX` - -diff ignores lines matching this regex (RE2) _(xff)_
- `--diff-format=u|c|n|y` - default -diff format: u/unified (default), c/context, n/normal, y/side-by-side _(xff)_
- `--diff-context=N` - default -diff context lines (3); overrides --context for -diff, and -diff=uN overrides it _(xff)_
- `--hash-algorithm=ALGO` - default digest for -hash / {hash} (sha256 default; md5, sha512, blake3, and more) _(xff)_
- `--hash-encoding=hex|base64` - default -hash / {hash} rendering: hex (default) or base64 _(xff)_
- `--path-encoding=raw|escape` - plain-output path byte encoding: raw (verbatim, default) or escape (C-escape controls) _(xff)_
- `--template=TEMPLATE` - render each match through a field template ({path}, {name}, ...) _(xff)_
- `--implicit-print=yes|no` - force the default -print on or off _(xff)_
- `--summary[=overall|type|ext|lang|mime|user|group|{template}]` - aligned count + size table (or --format=jsonl rows) instead of each match; repeatable _(xff)_
  Replaces the per-match listing with an aggregate table: match count and total size per group (overall, by type, extension, programming language, media (MIME) type, user (owner), or owning group). The categorical keys reuse the {mime}/{user}/{group} field vocabulary. A {template} key groups by any field value (e.g. --summary='{ext}-{type}'); a single m// extraction key (--summary='{capture.NAME:m/re/\1/}') groups per extracted line, so a per-file command's multi-line output tallies per key (e.g. git-blame lines per author) - the size column is not meaningful there. Repeatable: each --summary is its own table (e.g. --summary=ext --summary=type), printed in order. --top=N limits the rows of each, --summary-precision sets the scaled-size digits, and --format=jsonl emits one object per group for scripts.
- `--histogram=BUCKET[:MEASURE]` - bar chart per bucket: a count or sum/mean/min/max of size|lines (repeatable) _(xff)_
  A terminal reduction like --summary, drawn as bars. BUCKET groups the matches - a category (overall, type, ext, lang, mime, user (owner), or group) or a numeric-range field (size / lines by order of magnitude, depth per level, drawn as an ascending distribution). The optional :MEASURE is the bar's value - `count` (the default) or an aggregate `sum(FIELD)` / `mean(FIELD)` / `min(FIELD)` / `max(FIELD)` over a numeric FIELD (size or lines). A numeric metric needs an aggregator (`ext:lines` is an error; `ext:sum(lines)` is not). Repeatable and combinable with --summary - both are fed by one walk and replace the per-match listing. Bars scale to the tallest, use Unicode block characters on a UTF-8 locale (see --unicode) or ASCII '#' otherwise; --top=N keeps the N tallest and --format=jsonl emits one object per bar for scripts.
- `--count, -c` - with -grep, print a per-file matching-line count (path:count) instead of the lines _(xff)_
- `--context=SPEC` - -grep context lines: N both sides, or A:N,B:N,C:N for after/before/both (grep -C/-A/-B) _(xff)_
- `--after-context=N` - with -grep, print N lines of context after each match (grep -A; = --context=A:N) _(xff)_
- `--before-context=N` - with -grep, print N lines of context before each match (grep -B; = --context=B:N) _(xff)_
- `--top=N` - with --summary or --histogram, keep only the N largest/tallest groups _(xff)_
- `--histogram-width=N` - cell width the tallest --histogram bar fills (default 40) _(xff)_
- `--summary-precision=N` - with --summary --human: fraction digits for scaled sizes (default 2; bytes stay integer) _(xff)_
- `--color[=auto|always|never]` - colorize the plain listing by file type: auto (a tty), always, or never; honors NO_COLOR _(xff)_
  Colorizes the plain listing by file type. auto colorizes only when stdout is a terminal; always forces color even through a pipe or pager; never disables it. The NO_COLOR environment variable always wins.
- `--unicode[=auto|always|never]` - --format=tree connectors: auto (a UTF-8 locale), always (Unicode), or never (ASCII) _(xff)_
  Selects the box-drawing characters --format=tree connects nodes with. auto uses Unicode when the locale (LC_ALL / LC_CTYPE / LANG) is UTF-8, else ASCII; always forces the Unicode connectors; never forces the ASCII ones.
- `--human[=si|iec|off]` - size units for -ls / --summary: si (kB/MB, default), iec (KiB/MiB), off (bytes); xff -> si _(xff)_
- `--si` - human sizes in SI (kB/MB, 1000^N); an alias for --human=si (the --human default) _(xff)_
- `--buffer[=auto|off|all|N[kMG]|NMB]` - buffer to size columns (-ls / tables): auto, off, all, N[kMG] rows, or NMB/NMiB bytes _(xff)_

### Exit code control
- `--quiet, -q` - suppress output; exit 0 if anything matched, else 1 (-q: grep-compatible) _(xff)_
- `--exit-match` - keep output; exit 0 if anything matched, else 1 _(xff)_

### Safety
- `--safe` - refuse destructive actions (-delete / -exec) _(xff)_
- `--dry-run` - preview -delete without removing anything _(xff)_
- `--skip-unsupported` - warn and skip a predicate a filesystem cannot evaluate, not fail _(xff)_

### Fields & Exec
- `--exec-fields` - render -exec tokens through the field vocabulary ({name}, {path}, ...) _(xff)_
- `--define=NAME=VALUE` - define a value referenced as {def.NAME} _(xff)_
- `--capture-override` - allow a -capture NAME to be bound more than once (last wins) _(xff)_

### Time
- `--time-format=FMT` - default format for time fields (a preset name or a strftime pattern) _(xff)_
  Sets the default rendering for time fields ({mtime}, {atime}, -printf %t, ...) when no per-field qualifier is given. Accepts a preset (iso, epoch, space, find) or any strftime pattern such as %Y-%m-%d. A per-field qualifier like {mtime:%H:%M} still overrides it.
- `--timezone=ZONE, --tz=ZONE` - zone for interpreting/formatting times (local, utc, an IANA name, or +HH:MM) _(xff)_
  The zone used to interpret and format every time. Accepts local, utc, an IANA name like Europe/London, or a fixed offset like +02:00. Affects time fields and -newerXt comparisons.

## Expression

### Tests
- `-name ARG` - match the basename against a shell glob
  Globs the entry's basename (last path component): `*` matches any run including none, `?` one character, `[...]` a class. Unlike the shell a leading dot is matched literally. Case follows --case - the xff default folds when the volume does (APFS / HFS+ / NTFS), while --exact or --config=find forces a byte-exact compare; -iname always folds. Contrast -path (whole path) and -regex (anchored pattern). Example: `xff . -name '*.log'`.
- `-iname ARG` - match the basename against a shell glob, case-insensitively
  The always-case-insensitive -name: folds case regardless of --case or the volume.
- `-path ARG` - match the whole path against a shell glob
  Globs the whole path as printed (from the start point down), not just the basename. Unlike the shell, `*` and `?` DO match `/`, so `-path '*/build/*'` matches a build directory at any depth. Wildcards and case handling are -name's. GNU spells this -wholename.
- `-ipath ARG` - match the whole path against a shell glob, case-insensitively
  The always-case-insensitive -path (whole-path glob).
- `-wholename ARG` - GNU synonym for -path
- `-iwholename ARG` - GNU synonym for -ipath
- `-lname ARG` - match the symlink target against a shell glob
  Globs the symlink's target text - the path the link points AT, never the resolved destination - so a link matches even when its target is missing. Only a symbolic link can match, and with the default -P (or -H) a symlink is seen as itself. Wildcards and case handling are -name's; -ilname always folds.
- `-ilname ARG` - match the symlink target against a shell glob, case-insensitively
  The always-case-insensitive -lname (symlink-target glob).
- `-regex ARG` - match the whole path against a regular expression
  Matches when the pattern matches the WHOLE path (anchored both ends, like find), not just a substring - use `.*` to match anywhere. Dialect is chosen by -regextype (RE2 by default); capture groups become `{1}`..`{N}` for a following -exec / -printf. Example: `xff . -regex '.*/[0-9]+\.log'`.
- `-iregex ARG` - match the whole path against a regular expression, case-insensitively
  The case-insensitive -regex: same whole-path anchoring and capture-group binding, matching without regard to case.
- `-regextype ARG` - select the regex dialect for the following -regex/-iregex
- `-content ARG` - match a literal substring in the file's content (xff) _(xff)_
  Matches when the file contains SUBSTRING literally (no regex metacharacters - the literal pair sidesteps grep's flavor ambiguity). Reads the file, so it is expensive; a non-regular, unreadable, or binary file (a NUL byte in the first 8 KiB) never matches. -icontent folds ASCII case. Use -rxc for a pattern. This is an xff extension --config=find rejects.
- `-icontent ARG` - match a literal substring in the file's content, case-insensitively (xff) _(xff)_
  The case-insensitive -content: folds ASCII case on the literal substring search.
- `-rxc ARG` - match the file's content against a regular expression (xff) _(xff)_
  The regex counterpart of -content: matches when the RE2 pattern is found ANYWHERE in the content (unanchored, like grep - use `^` / `$` to anchor), not the whole-file anchoring -regex applies to the path. Same expensive read and non-regular / unreadable / binary skip; -irxc folds case. An xff extension --config=find rejects.
- `-irxc ARG` - match the file's content against a regular expression, case-insensitively (xff) _(xff)_
  The case-insensitive -rxc: folds case on the content regex search.
- `-text` - match a regular text file; -text[=git|posix|windows|apple] picks the definition (xff) _(xff)_
  TRUE for a regular, readable file whose content is text. Bare -text (or =git) is the default heuristic: no NUL byte in the first 8000 bytes (git's buffer_is_binary, also grep/ripgrep), line-ending-agnostic. The strict flavors forbid a NUL ANYWHERE and pin the line ending, requiring a final terminator (an empty file is vacuously complete): =posix = LF only, ends with a newline; =windows = CRLF only; =apple = CR only. Reads the file (expensive). A directory, symlink, device or unreadable file is not text (nor binary), so it never matches - `! -text` is NOT -binary. An xff extension --config=find rejects.
- `-binary` - match a regular file whose content is binary (a NUL in the first 8 KiB) (xff) _(xff)_
  TRUE for a regular, readable file whose content is binary - a NUL byte in the first 8 KiB. The precise complement of -text WITHIN regular files: a directory, symlink, device or unreadable file is neither, so `-binary` is not `! -text`. Reads the file (expensive). An xff extension --config=find rejects.
- `-eofnl` - match a regular file whose content ends with a newline (LF), or is empty (xff) _(xff)_
  TRUE for a regular, readable file whose content ends with a newline / LF (or is empty - a zero-line file is complete). Tests ONLY the final terminator, the other axis from -text/-binary: compose -text -eofnl for a well-formed (POSIX-style) text file, or -text ! -eofnl for the common lint 'a text file missing its final newline'. A CRLF file ends with LF too, so it also matches -eofnl; -eofcrlf is the strict CRLF form. Reads the file (expensive). An xff extension --config=find rejects.
- `-eofcr` - match a regular file whose content ends with a bare CR, or is empty (xff) _(xff)_
  TRUE for a regular, readable file whose content ends with a bare carriage return / CR (or is empty). The classic-Mac / -text=apple final terminator, and the CR analogue of -eofnl: compose -text=apple -eofcr for a well-formed CR-terminated file, or -text=apple ! -eofcr for the missing final CR. A CRLF file ends with LF (not a bare CR), so it does NOT match -eofcr. Reads the file (expensive). An xff extension --config=find rejects.
- `-eofcrlf` - match a regular file whose content ends with CRLF, or is empty (xff) _(xff)_
  TRUE for a regular, readable file whose content ends with CRLF (or is empty). The Windows / -text=windows final terminator, and the CRLF analogue of -eofnl: compose -text=windows -eofcrlf for a well-formed CRLF-terminated file, or -text=windows ! -eofcrlf for the missing final CRLF. Stricter than -eofnl (which any LF-ending file, including CRLF, satisfies). Reads the file (expensive). An xff extension --config=find rejects.
- `-cmp ARG` - true when the file's content is byte-identical to TARGET (a field template) (xff) _(xff)_
- `-type ARG` - match the file type (f/d/l/b/c/p/s)
  Matches the entry's type by letter: f=regular file, d=directory, l=symlink, b/c=block / char device, p=FIFO, s=socket. A GNU-style comma list is any-of, so `-type f,l` matches regular files or symlinks. Under the default -P a symlink is type l; -xtype tests its target's type instead.
- `-xtype ARG` - match the file type of a symlink's target
  Like -type, but for a symlink it tests the type of the link's TARGET (the link is followed). A broken symlink has no target, so it reports as a symlink and `-xtype l` matches it, matching GNU find under the default -P. On a non-symlink it is identical to -type.
- `-mime ARG` - match the media type by extension against a glob, e.g. -mime 'image/*' (xff) _(xff)_
  xff extension: matches the media (MIME) type derived from the filename extension (a fast, dependency-free table - no content sniffing) against a shell glob, so `image/*` matches png/jpg/... and `text/plain` is exact. The same value is the {mime} field. Matching is always case-insensitive (MIME names are case-insensitive per RFC 2045/6838), so `IMAGE/*` behaves like `image/*`; --case / -i / -s do not affect it.
- `-lang ARG` - match the language by extension/filename against a glob, e.g. -lang 'C*' (xff) _(xff)_
  xff extension: matches the programming language inferred from the extension/filename (github-linguist data) against a shell glob, so `C*` matches C / C++ / C#. The same value is the {lang} field. Matching is always case-insensitive (`c++` matches the canonical `C++`) and unaffected by --case / -i / -s.
- `-size ARG` - match the apparent size (unit suffix c/w/k/M/G/T/P/E)
  Compares the file's apparent size. A bare number counts 512-byte blocks (find default); a unit suffix sets the scale - c=bytes, w=2 bytes, k/M/G/T/P, plus the xff-only E. A leading + / - means greater / less than. Following GNU, the size is rounded up to whole units, so `-size +100M` means "larger than 100 MB". (See -blocks for allocated space.)
- `-blocks ARG` - match the allocated size (st_blocks); xff's disk-occupancy counterpart to -size _(xff)_
- `-links ARG` - match the hard-link count
- `-inum ARG` - match the inode number
- `-samefile ARG` - match files that share an inode with FILE
- `-fstype ARG` - match the filesystem type (statfs)
  Matches when the filesystem holding the entry has the given type name (e.g. `apfs`, `ext2/ext3`, `tmpfs`, `nfs`). The recognized names are platform-specific - macOS / BSD report `f_fstypename` verbatim, Linux maps the statfs magic to a find-compatible name - so a portable expression usually cannot assume one name across OSes.
- `-uid ARG` - match the numeric owner id
  Matches the owner's numeric user id. Like find's numeric tests it accepts `+N` (greater than), `-N` (less than), or a bare N (exact). Match by login name with -user instead.
- `-gid ARG` - match the numeric group id
  The group counterpart of -uid: the numeric group id, with `+N` / `-N` / bare-N. Match by group name with -group instead.
- `-user ARG` - match the owner by name
  Matches the owner by login name, resolved through the passwd database. A name with no passwd entry never matches, but a bare numeric argument is taken as a uid, so `-user 0` behaves like `-uid 0`. Exact match only (no `+` / `-`).
- `-group ARG` - match the group by name
  The group counterpart of -user: matches by group name (via the group database), falling back to a numeric gid. Exact match only.
- `-nouser` - match when the owner uid has no passwd entry
  Matches when the entry's owner uid has NO entry in the passwd database - an orphaned owner, e.g. from a deleted account or an archive unpacked with foreign ids. Takes no argument. See -nogroup for the group side.
- `-nogroup` - match when the group gid has no group entry
  Matches when the entry's group gid has no entry in the group database (the group side of -nouser).
- `-newer ARG` - match when mtime is newer than the reference file's mtime
  Matches when the entry's mtime is strictly newer than reference FILE's mtime. FILE is stat'd following symlinks; a missing or unreadable reference makes it false. This is the base of the -newerXY family: `-newerXY FILE` compares the entry's X time against the reference's Y time, where each of X and Y is a=access, c=status-change, m=modification, or B=birth - so `-newerac` is the entry's atime vs the reference's ctime. -anewer / -cnewer are the classic aliases. When Y is `t` the operand is a TIME STRING, not a file (see -newermt). A birth time the filesystem never recorded makes an X=B test a hard error and a Y=B reference a silent no-match.
- `-anewer ARG` - match when atime is newer than the reference file's mtime (== -neweram)
  find's classic spelling of -neweram: the entry's access time is newer than the reference file's modification time. See -newer for the -newerXY family.
- `-cnewer ARG` - match when ctime is newer than the reference file's mtime (== -newercm)
  find's classic spelling of -newercm: the entry's status-change time is newer than the reference file's modification time. See -newer for the -newerXY family.
- `-neweraa ARG` - match when atime is newer than the reference file's atime
- `-newerac ARG` - match when atime is newer than the reference file's ctime
- `-neweram ARG` - match when atime is newer than the reference file's mtime
- `-newerca ARG` - match when ctime is newer than the reference file's atime
- `-newercc ARG` - match when ctime is newer than the reference file's ctime
- `-newercm ARG` - match when ctime is newer than the reference file's mtime
- `-newerma ARG` - match when mtime is newer than the reference file's atime
- `-newermc ARG` - match when mtime is newer than the reference file's ctime
- `-newermm ARG` - match when mtime is newer than the reference file's mtime
- `-newerat ARG` - match when atime is newer than a time string
- `-newerct ARG` - match when ctime is newer than a time string
- `-newermt ARG` - match when mtime is newer than a time string
  The `-newerXt` time-string form: matches when the entry's mtime is newer than TIME - a timestamp xff parses (an ISO date / date-time, @epoch, or a relative span), interpreted in --timezone - rather than a reference file. -newerat / -newerct / -newerBt are the access / status-change / birth-time counterparts; the file-reference forms are -newerXY (see -newer).
- `-newerBa ARG` - match when birth time is newer than the reference file's atime
- `-newerBc ARG` - match when birth time is newer than the reference file's ctime
- `-newerBm ARG` - match when birth time is newer than the reference file's mtime
- `-newerBB ARG` - match when birth time is newer than the reference file's birth time
- `-newerBt ARG` - match when birth time is newer than a time string
- `-neweraB ARG` - match when atime is newer than the reference file's birth time
- `-newercB ARG` - match when ctime is newer than the reference file's birth time
- `-newermB ARG` - match when mtime is newer than the reference file's birth time
- `-mtime ARG` - match the data-modification age in days
  Matches the data-modification age. A bare integer N counts 24-hour periods with any fraction floored (a 2.9-day file is 2); `+N` matches strictly older than N units, `-N` strictly younger. A trailing s/m/h/d/w overrides the unit BSD-style (`-mtime -1h` = under an hour old). The xff-only word/compound span (`-mtime "-3 weeks 3 hours"`, sign required) reaches back a full relative duration and is rejected by --config=find. See -mmin for the minute scale, -atime / -ctime / -Btime for the other time axes.
- `-mmin ARG` - match the data-modification age in minutes
  The minute-scale -mtime: N counts whole minutes (floored), `+N` / `-N` for older / younger. Integer only - no unit suffix and no compound span (use -mtime for those).
- `-atime ARG` - match the access age in days
  -mtime measured on the access time (atime): same N-day scale, `+N` / `-N` polarity, BSD unit suffix, and xff compound span. Note atime is often unreliable - many mounts use relatime or noatime, so a read may not update it.
- `-amin ARG` - match the access age in minutes
  The minute-scale -atime (access time): integer minutes, `+N` / `-N`, no suffix. See -mmin.
- `-ctime ARG` - match the status-change age in days
  -mtime measured on the status-change time (ctime) - when the inode metadata last changed (permissions, ownership, link count, rename), which a content edit also bumps. Same N-day scale, `+N` / `-N` polarity, BSD unit suffix, and xff compound span. This is not a creation time; see -Btime for that.
- `-cmin ARG` - match the status-change age in minutes
  The minute-scale -ctime (status-change time): integer minutes, `+N` / `-N`, no suffix. See -mmin.
- `-Btime ARG` - match the birth (creation) age in days
  -mtime measured on the birth (creation) time: same N-day scale, `+N` / `-N` polarity, BSD unit suffix, and xff compound span. Birth time is not recorded on every filesystem or kernel - where it is absent the test cannot be evaluated and is a hard error (exit 2); --skip-unsupported downgrades that to a warning and skips the entry.
- `-Bmin ARG` - match the birth (creation) age in minutes
  The minute-scale -Btime (birth time): integer minutes, `+N` / `-N`, no suffix. Same unrecorded-birth-time handling as -Btime (hard error, or a skip under --skip-unsupported).
- `-used ARG` - match the whole days between atime and ctime
  Matches the whole days between an entry's last status change and its last access (atime minus ctime) - roughly how long after its metadata changed it was next read. `+N` / `-N` for more / fewer days. Shares atime's relatime / noatime caveat (see -atime).
- `-perm ARG` - match the permission bits (octal or symbolic mode)
  Matches the permission (and setuid / setgid / sticky) bits. MODE is octal (`644`, `0755`) or a chmod-style symbolic mode (`u+w`, `go=r`, comma-separated clauses). A bare MODE matches exactly; `-MODE` matches when ALL the listed bits are set; `/MODE` (GNU) when ANY are. BSD `+octal` is any-of like `/`, while a symbolic `+r` stays exact. Example: `-perm -u+x` = owner-executable. Contrast -readable / -writable / -executable, which probe the effective user's real access.
- `-maxdepth ARG` - descend at most N directory levels below each start
  Limits traversal to at most N levels below each start point: level 0 is a start point itself, 1 its immediate children. Like find this is a global positional option - it applies to the whole run wherever it sits in the expression, not just to what follows it. Pair with -mindepth to bound both ends.
- `-mindepth ARG` - skip entries fewer than N levels below each start
  Skips entries fewer than N levels below a start point, so -mindepth 1 excludes the start points themselves. A global positional option like -maxdepth (applies run-wide).
- `-depth` - process a directory's contents before the directory
  Visits a directory's contents BEFORE the directory itself (post-order), so a directory is acted on only after everything within it - what -delete needs, and -delete turns this on for you. A global positional option; -d is the BSD/GNU short spelling.
- `-d` - BSD/GNU short spelling of -depth
- `-xdev` - do not descend into other filesystems
  Confines the walk to the filesystem of each start point: it will not descend into a directory that lives on a different mounted device. A global positional option; -mount and -x are synonyms.
- `-mount` - GNU/BSD synonym for -xdev
- `-x` - BSD synonym for -xdev
- `-daystart` - measure age tests from today's local midnight
  Measures the day- and minute-scale age tests (-mtime / -atime / -ctime / -Btime and their -min forms) from the start of today (local midnight) instead of from the exact current instant, matching GNU find's -daystart. Unlike find, where it only affects tests to its right, in xff it applies run-wide regardless of where it appears in the expression.
- `-ignore_readdir_race` - skip entries that vanish during the walk (ENOENT)
- `-noignore_readdir_race` - report vanished entries as errors (default)
- `-empty` - match an empty regular file or empty directory
  Matches an empty regular file (size 0) or a directory with no entries; other types never match. The directory case reads the directory to check, so it costs a syscall.
- `-sparse` - match a file with holes (allocated blocks < apparent size)
  Matches a file stored sparsely - fewer 512-byte blocks are allocated than its apparent size would need (`st_blocks * 512 < st_size`), i.e. it has holes. A zero-size file is never sparse. Compare -blocks (allocated space) against -size (apparent size).
- `-readable` - match entries the current user can read
  Matches when the entry is readable by the CURRENT (effective) user, via a real access(2) probe rather than a guess from the mode bits - so it reflects ownership and ACLs and can differ from -perm. See -writable / -executable for the other access modes.
- `-writable` - match entries the current user can write
  The write-mode -readable: a real access(2) probe for the effective user (see -readable).
- `-executable` - match entries the current user can execute
  The execute/search-mode -readable: a real access(2) probe for the effective user. On a directory this means search (traverse) permission. See -readable.
- `-true` - always match
- `-false` - never match

### Actions
- `-diff ARG` - diff the file against TARGET (a field template); true when equal (xff) _(xff)_
  Compares the matched file against TARGET - a {field} template evaluated per entry, so it can name a mirror path like `../b/{relpath}` - and is true when they are equal, false on a difference. The optional =STYLE picks the output: unified `u3` (default; 3 lines of context), context `c`, normal `n`, side-by-side `y`, or `none` for just the boolean. Text files only; expensive.
- `-hash` - print the file digest and path; -hash=ALGO[/ENCODING], sha256 hex default (xff) _(xff)_
  Prints `DIGEST  PATH` for each match (an action). `-hash=ALGO[/ENCODING]` picks the algorithm (sha256 default; also sha1/sha512/...) and encoding (hex default, or base64). Reads the whole file, so it is expensive; the same digest is available as the {hash} field.
- `-ls` - print an `ls -dils` style line per entry
  Prints one `ls -dils`-style line per match: inode, blocks, mode, links, owner, group, size, time, name (find's -ls). Columns align to ls/BSD width defaults. For a custom layout use -printf; for aligned columns of {field}s use --format=aligned.
- `-print` - print the path followed by a newline
  Prints the path then a newline. This is the DEFAULT action: with no action anywhere in the expression xff prints each match, exactly as if -print were appended. Naming any action (including -print itself) suppresses that implicit default; --implicit-print=yes|no forces it on or off.
- `-print0` - print the path followed by a NUL
  Prints the path then a NUL byte instead of a newline, so paths containing spaces or newlines survive a pipe into `xargs -0`. The machine-readable counterpart of -print; see also --format=jsonl.
- `-printf ARG` - print a custom format string (%{field} expands the xff field vocabulary)
  Prints FORMAT for each match, expanding find's `%` directives (%p path, %f name, %s size, %t/%Ak times, ...) and C escapes (\n, \t). xff adds `%{NAME}` to reach the full {field} vocabulary and its qualifiers (see --help=fields, --help=printf). No trailing newline unless you write one; -printfln adds the OS line ending. Example: `xff . -printf '%s\t%p\n'`.
- `-println` - print the path with the OS line ending (xff) _(xff)_
  -print but terminated with the OS-native line ending (CRLF on Windows, LF elsewhere) rather than always LF. An xff extension --config=find rejects.
- `-printfln ARG` - print a custom format with the OS line ending (xff) _(xff)_
  -printf plus the OS line ending appended, so you write FORMAT without a trailing `\n`. An xff extension --config=find rejects; see -printf for the directive vocabulary.
- `-grep ARG` - print each content line matching a regex; -grep=FORMAT for a template (xff) _(xff)_
  The line-output companion of -rxc: `-grep PATTERN` prints every content line matching the RE2 PATTERN as `path:lineno:text` (grep's piped form; a literal substring under --regextype=EXACT). `-grep=FORMAT PATTERN` renders a {line}/{text}/{match}/{column} template instead. Honors -c / --count (one `path:count` per file) and -A / -B / --context (surrounding lines, grep-style). Reads the file (expensive); non-regular / unreadable / binary files yield nothing. Its truth is "matched a line", so it composes with -o / -q. An xff extension --config=find rejects.
- `-fprint ARG` - write -print output to a named file
  Writes what -print would emit to FILE instead of stdout. FILE is opened once (truncating any existing content) and held open for the whole walk, so matches append to it in visit order. This is the anchor of the -f* family - each mirrors a stdout action: -fprint0, -fprintf, -fls, and the xff -fprintln / -fprintfln.
- `-fprintln ARG` - write -println output to a named file (xff) _(xff)_
  The file form of -println (-fprint with the OS line ending). See -fprint for the file handling; an xff extension --config=find rejects.
- `-fprint0 ARG` - write -print0 output to a named file
  The file form of -print0 (NUL-terminated paths). See -fprint for the file handling.
- `-fprintf ARG ARG` - write -printf output to a named file
  The file form of -printf: `-fprintf FILE FORMAT` (FILE first, then the format). See -printf for the directive vocabulary and -fprint for the file handling.
- `-fprintfln ARG ARG` - write -printfln output to a named file (xff) _(xff)_
  The file form of -printfln: `-fprintfln FILE FORMAT` with the OS line ending appended. An xff extension --config=find rejects; see -fprint for the file handling.
- `-fls ARG` - write -ls output to a named file
  The file form of -ls (the `ls -dils` line). See -fprint for the file handling.
- `-delete` - delete the matched entry
  Deletes the matched file or (empty) directory, and implies -depth so a directory's contents are removed before the directory itself. Destructive, so it is guarded: --dry-run previews (prints what would be deleted, removes nothing) and --safe refuses risky targets. Example: `xff . -name '*.tmp' -delete`.
- `-prune` - do not descend into the matched directory
  When the matched entry is a directory, do not descend into it (evaluates true). Usually paired with -o to skip a subtree while still processing everything else: `xff . -name .git -prune -o -print`.
- `-quit` - stop the search immediately
  Stops the whole search as soon as it is reached (after actions on the current entry have run). Handy to emit just the first match: `xff . -name target -print -quit`.
- `-exec CMD... ;` - run a command per match (;) or batched (+)
  Runs the command up to a terminator: `;` runs it once per match, `+` batches as many paths as fit per invocation (like xargs). `{}` expands to the path; xff also binds `{1}`..`{N}` from -regex capture groups and the whole {field} vocabulary. Serial by default; `-j N` runs invocations in parallel. Sensitive: loaded from an --xffrc file it needs --allow-exec. Example: `xff . -name '*.o' -exec rm {} +`.
- `-execdir CMD... ;` - run a command in the matched entry's directory
  Like -exec, but each command runs with its working directory set to the matched entry's parent and `{}` is the basename - safer against path injection and directory races. `;` per match or `+` batched (a batch shares one directory). Example: `xff . -name '*.log' -execdir gzip {} ;`.
- `-ok CMD... ;` - like -exec, but prompt before each command
  Like -exec but prompts on stderr before each command and runs it only when the reply begins with 'y'; a declined or EOF answer skips that entry. `;`-terminated only (no `+` batching, since each run needs its own prompt).
- `-okdir CMD... ;` - like -execdir, but prompt before each command
  Like -execdir (runs in the matched entry's directory, `{}` is the basename) but prompts before each command, exactly as -ok does.
- `-capture=NAME[=REGEX] CMD... ;` - run a command and bind its output to {capture.NAME} (xff) _(xff)_
  xff extension: runs the `;`-terminated command and binds its stdout to `{capture.NAME}` for a later -printf / --format field; `-capture=NAME=REGEX` keeps only REGEX's first capture group. Sensitive: from an --xffrc file it needs --allow-exec. Example: `-capture=branch git rev-parse --abbrev-ref HEAD ; -printf '{relpath}\t{capture.branch}\n'`.
- `-capturedir=NAME[=REGEX] CMD... ;` - run -capture in the matched entry's directory (xff) _(xff)_
  The -execdir counterpart of -capture: runs the command in the matched entry's directory and binds its stdout to `{capture.NAME}`. Same `NAME[=REGEX]` binding and --allow-exec gating.

### Operators
- `-a` - logical AND (implicit between predicates)
  Logical AND of two predicates (`-and` is the long spelling). It is also IMPLICIT between juxtaposed predicates, so `-type f -name '*.c'` means `-type f -a -name '*.c'`. Precedence, tightest to loosest: -not, then -a, then (xff) -xor, then -o, then the `,` comma operator; parentheses `( ... )` override it. Evaluation short-circuits.
- `-and` - logical AND (implicit between predicates)
- `-o` - logical OR
  Logical OR of two predicates (`-or` is the long spelling); binds looser than -a, so `A -o B -a C` is `A -o (B -a C)`. Short-circuits: the right side is skipped when the left already matched. See -a for the full precedence order.
- `-or` - logical OR
- `-not` - logical negation
  Negates the predicate that follows (`!` is the synonym). Binds tightest of the operators, so `-not -type d -o -name x` is `(-not -type d) -o -name x`. See -a for the full precedence order.
- `!` - logical negation
- `-xor` - logical XOR; matches exactly one side (xff) _(xff)_
  Matches when exactly ONE side is true (never both). One of four xff-only operators find lacks: -xor, and the negations -nand (not both), -nor (neither), -xnor (both agree). They sit between -a and -o in precedence (-not > -a / -nand > -xor / -xnor > -o / -nor) and, like all xff-only operators, are rejected by --config=find.
- `-nand` - logical NAND; ! (lhs -a rhs) (xff) _(xff)_
- `-nor` - logical NOR; ! (lhs -o rhs) (xff) _(xff)_
- `-xnor` - logical XNOR; matches when both sides agree (xff) _(xff)_

## Fields

The `{field}` placeholder vocabulary, substituted per entry in --template / --format, in -printf via the `%{field}` escape, and (with --exec-fields) in -exec.

### Path & name

- `{path}` - full path as traversed ({} is an alias)
- `{relpath}` - path relative to the search root (find %P)
- `{root}` - the search root it was reached from (find %H)
- `{dir}` - directory containing the entry
- `{name} {file}` - final path component (the file name)
- `{stem}` - name without its last extension
- `{core}` - name without all extensions (foo.tar.gz -> foo)
- `{ext} {extension}` - last extension, no dot (gz)
- `{suffix}` - last extension, with dot (.gz)
- `{suffixes}` - all extensions, with dots (.tar.gz)
- `{target}` - a symlink's target (find %l); else empty

### Type & size

- `{type}` - entry type letter (f, d, l, ...)
- `{lang} {language}` - language by extension/filename (C++, Python, ...; empty if unknown)
- `{mime}` - media (MIME) type by extension (text/plain, image/png; application/octet-stream if unknown)
- `{size}` - size in bytes ({size:h} human-readable)
- `{blocks}` - 512-byte blocks allocated
- `{inode}` - inode number
- `{links}` - hard-link count
- `{dev}` - device number
- `{depth}` - depth below the root (0 at a root operand)

### Content

- `{hash}` - file digest; {hash:ALGO[/ENCODING]} picks the algorithm (default sha256) and hex/base64
- `{lines}` - text line count (empty for a binary/unreadable file); reads the file

### Owner & mode

- `{user} {owner}` - owner user name (alias {owner}; find %u)
- `{group}` - owner group name
- `{uid}` - owner numeric user id
- `{gid}` - owner numeric group id
- `{mode} {perm}` - permission bits, octal
- `{access}` - symbolic permissions (ls -l / stat %A)

### Time

- `{atime}` - last access time
- `{mtime}` - last modification time
- `{ctime}` - inode change time
- `{btime}` - creation/birth time (where supported)

### Grep context

- `{line}` - 1-based number of the matching line
- `{text}` - the full matching line
- `{match}` - the matched substring (grep -o)
- `{column}` - 1-based column of the match

### Braces

- `{{` and `}}` emit literal braces
- `{}` is an alias for `{path}`
- an unknown field renders empty
- a malformed or unterminated `{` stays literal

### Dynamic namespaces

- `{0}..{N}` - -regex captures ({0} the whole match, {1}..{N} the groups)
- `{env.NAME}` - a process environment variable
- `{def.NAME}` - a --define value
- `{capture.NAME}` - a -capture command result

### Qualifiers ({field:QUAL})

- `{mtime:FMT}` - time format: strftime (%Y-%m-%d) or preset (iso, epoch); see --time-format / --timezone
- `{size:h}` - human-readable size
- `{name:s/RE/R/f}` - RE2 rewrite of the value (flags g=all, i=ignore-case; any delimiter)
- `{cap:m/RE/R/f}` - per-line extraction: a value stream, e.g. a --summary key (m//, s///'s list-producing sibling)
- `{cap:m/RE/R/;join(SEP)}` - reduce the stream to one scalar (join, SEP default newline) so m// is usable in a scalar context (-printf / --template / -exec); reducers are function-notation, e.g. join(, )
- `{path:COMP}` - path component of the value: basename|core|dir|ext|extension|file|name|path|stem|suffix|suffixes; any path-valued field composes, e.g. {relpath:stem}, {def.B:dir}

An m// extraction is a left-to-right pipeline: s/// maps whatever is flowing (each line, then the scalar), and a terminal reducer such as join collapses the stream to one scalar.

```
  {cap:m/PAT/REP/;s/PAT/REP/;join(SEP);s/PAT/REP/}
       |________| |________| |_______| |________|
       extract    map each   reduce    rewrite
       per line   line       stream    scalar
```

For -printf's own % directives (%p %f %s %t ...) and the `%{field}` escape that bridges them to this vocabulary, see the Printf directives section below.

## Printf directives

Directives for -printf / -fprintf / -println FORMAT, and the `%{field}` escape.

- `%p` - the entry's path
- `%f` - file name (basename)
- `%h` - leading directories (dirname)
- `%d` - depth below the starting point
- `%y` - type letter (f d l b c p s)
- `%s` - size in bytes
- `%i` - inode number
- `%n` - number of hard links
- `%m` - permission bits, octal
- `%u` - owner user name (numeric uid if unknown)
- `%g` - owner group name (numeric gid if unknown)
- `%U` - numeric user id
- `%G` - numeric group id
- `%a %c %t` - access / change / modification time (asctime form)
- `%Ak %Ck %Tk` - atime / ctime / mtime via strftime conversion k (e.g. %TY, %Tj)
- `%%` - a literal percent
- `\n \t \r \\ \0` - newline, tab, carriage return, backslash, NUL
- `%{NAME}` - xff: the {field} vocabulary (%{relpath}, %{size:h}, %{def.X}); see --help=fields
- `%{NAME:qual}` - xff: a field with a :qualifier -- time format, {size:h}, s/// rewrite, or path component (see --help=fields for the full qualifier list)

## Time formats

Presets and strftime patterns for --time-format, --timezone, and time-field {:qualifiers}.

- `iso, iso8601` - ISO-8601 extended (2020-09-13T12:26:40+0000)
- `iso8601-basic` - ISO-8601 basic / compact (20200913T122640+0000)
- `iso8601-full` - ISO-8601 with sub-second precision
- `rfc3339` - RFC 3339, colon offset (2020-09-13T12:26:40+00:00)
- `space, human` - readable default (2020-09-13 12:26:40 +0000)
- `asctime` - asctime(3); find's default %t (Sun Sep 13 12:26:40 2020)
- `epoch` - seconds since the Unix epoch
- `zulu` - UTC with a Z designator (2020-09-13T12:26:40Z)
- `zulu-dense` - UTC Z, no separators (20200913T122640Z)
- `<strftime>` - any other value is used as an strftime(3) pattern, e.g. %Y-%m-%d

## Size units

Units for -size / -blocks [+|-]N[unit].

- `c` - bytes
- `w` - 2-byte words
- `b` - 512-byte blocks (the default unit; --block-size overrides)
- `k` - kibibytes (1024 bytes)
- `M` - mebibytes (1024^2)
- `G` - gibibytes (1024^3)
- `T` - tebibytes (1024^4)
- `P` - pebibytes (1024^5)
- `E` - exbibytes (1024^6)
- `+N / -N` - greater than / less than N units; a bare N matches exactly

## Regex grammars

The grammar for -regex / -iregex and the content matchers -rxc / -grep, chosen by `--regextype` (default RE2). EXACT, FNMATCH, GLOB and SHGLOB are core engines, always built in; PCRE2 is a build-time extra (see `--help=extras`). RE2 and PCRE2 have canonical external references, so the smaller engines are spelled out in full here: they have no single authoritative man page, and FNMATCH delegates to the platform's fnmatch(3), whose class / collation details vary by system.

- `RE2` - the default. Google RE2 regular expressions - linear-time, no catastrophic backtracking. Full syntax: https://github.com/google/re2/wiki/Syntax .
- `EXACT` - a literal string; every character matches itself, no metacharacters. -regex is whole-string equality, -rxc / -grep a substring test.
- `FNMATCH` - a flat shell wildcard via the platform's fnmatch(3): * matches any run of characters (including /), ? one character, [...] a class. Whole-string, like find -name / -path (no /-awareness); -i uses FNM_CASEFOLD. Provided by libc, so class / collation details vary by system.
- `GLOB` - xff's own path-aware shell glob (gitignore-flavored, compiled to RE2 - NOT POSIX glob(7)): * and ? match within one path segment (they stop at /); ** is a whole-segment cross-directory wildcard (leading **/ = zero or more directories, trailing /** = everything below, a glued ** degrades to *); [...] is a class with [a-z] ranges, [[:alpha:]] POSIX classes, a leading ! negating and a leading ] literal; { } are literal. Because it compiles to RE2, -grep / -rxc partial matching and match spans work.
- `SHGLOB` - GLOB plus brace alternation: {a,b,c} matches any one alternative, so *.{cc,h} matches either. Alternatives may nest and may be empty; each is itself SHGLOB-translated. \{ \} \, and braces inside a [...] class are literal. Everything else is exactly GLOB.
- `PCRE2` - Perl-Compatible Regular Expressions (lookaround, backreferences, ...). A build-time extra: present only in a full build - run `xff --help=extras` to see whether THIS binary has it. Full syntax: pcre2pattern(3).

## Examples

```
xff cookbook: worked examples that compose xff's building blocks. Each shows a task, the
command, and how it works. See --help=fields for {field}s and --help=stats for the reductions.

  Ten largest files
    xff . -type f -printf '%s\t%p\n' | sort -rn | head
    %s is the size, %p the path; the shell sorts and takes the top ten. -printf builds any columnar line you need.

  Disk use per file type
    xff . -type f --summary=ext
    a count + total size per extension; the --summary global reads naturally at the end, after the expression (a --long global may sit anywhere). Swap in --histogram=ext for bars, or --histogram='ext:sum(lines)' to rank by lines. See --help=stats.

  Delete stale temp files, safely
    xff . -type f -name '*.tmp' -mtime +7 -delete --dry-run
    lists what -delete WOULD remove (guarded by --dry-run); rerun without it to delete. -delete implies -depth so directories empty first.

  Search code content, filtered by language
    xff src -lang 'C*' -grep 'TODO'
    prints every TODO line as path:lineno:text in C / C++ / C# files; add -c for per-file counts or --context=2 for surrounding lines.

  Per-file git-blame author line counts
    xff . -text -exec git blame --line-porcelain {} \; | grep '^author ' | sort | uniq -c | sort -rn
    runs git blame on each text file; the shell pipe tallies lines per author across the tree. -text skips binaries (which git blame cannot line-blame). -exec feeds any pipeline the field vocabulary cannot express alone.

  Author line counts, natively (no shell pipe)
    xff -g . -text -capturedir=blame git blame --line-porcelain {} \; --summary='{capture.blame:m/^author (.+)$/\1/}'
    the recipe above with the awk|sort tail folded into xff. -capturedir runs git blame in each file's own directory (repo-safe, works across nested repos); --summary folds that output via an m// extraction, tallying lines per author across the tree - no external pipe. -g honors .gitignore and skips .git; -text keeps blame off binaries. Pass several roots (a b c ...) to span multiple trees. A single-dash global like -g leads; double-dash globals such as --summary may sit anywhere (before or after the paths).

  Checksum manifest for a tree
    xff . -type f -hash=sha256
    prints `DIGEST  PATH` per file (like sha256sum); redirect to a file to snapshot a tree, then diff two runs to spot changes.

  Recently changed files as machine rows
    xff . -type f -mtime -1 --format=jsonl
    everything modified in the last day, one JSON object per file, ready for jq or a script.

```

## Exit status

0 on success, 2 on error. With `--quiet` or `--exit-match` the exit is 0 when something matched and 1 when nothing did (an error still outranks the match status).

## See also

`find`(1), `grep`(1), `fnmatch`(3), `glob`(7), `pcre2pattern`(3)

For the `--regextype` grammars see the Regex grammars section above (`--help=grammars`). FNMATCH is the platform's fnmatch(3) and PCRE2 is pcre2pattern(3); GLOB and SHGLOB are xff's own path-aware globs (compiled to RE2), NOT POSIX glob(7) - that page is listed only as background on shell globbing. The default RE2 grammar has no man page; its syntax is at https://github.com/google/re2/wiki/Syntax .
