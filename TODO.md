# TODO

Open, cross-cutting decisions to revisit. Code-level TODOs live in comments;
deferred features live in the CHANGELOG. This file is for design choices that are
shipped one way but not yet settled.

## Open decisions

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

## Remaining work

The backlog of features and infrastructure not yet built. Ordered by current
intent, not hard dependency. Task numbers reference the agent task list.

### Lint / CI / style adoption (from helly25/mbo)

- **Style docs + `.clang-tidy`** (this change). `.clang-tidy` (mbo's rule set),
  `STYLE_CPP.md`, `RULES.md`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, and an
  `AGENTS.md` pointer. The `bazel-compile-commands-extractor` dev module is
  already wired (`bazelmod/dev.MODULE.bazel`), so clang-tidy can run locally.
- **Apply clang-tidy.** `bazel run @bazel_compile_commands_extractor//:refresh_all`
  to produce `compile_commands.json`, run `clang-tidy` across `xff/`, and fix the
  findings (likely several PRs sized by finding count).
- **Adopt trunk.** `.trunk/trunk.yaml` (+ `configs/`) and a CI `trunk` job
  mirroring mbo: clang-tidy (CI-skipped without a compile DB, gated locally),
  buildifier, markdownlint, prettier, yamllint, trivy/trufflehog, git-diff-check.
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
  bundle of feature defaults (design-config.md L162-165). **Trigger (also in
  AGENTS.md):** the first boolean user-toggleable capability that is neither a style
  behavior nor a valued option must be built as the first `--feature`, not a bespoke
  flag.
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
- **Hash-verification workflow (#109) - [DISCUSS].** The hashing primitives shipped (#105:
  `xff/hash` + the `{hash}` / `{hash:sha256}` field + the `-hash` action + hex/base64 via
  `mbo::digest`). Still to design and build: read an expected hash into a variable, an
  `-eval`-style matcher that compares a computed `{hash}` against it (verify a manifest, detect
  drift), and `--summary` tallies of verified vs failed (plus `--summary=hash` grouping for
  dedup). **Open:** where expected hashes come from (a sidecar manifest file, a `{def.X}` value,
  or a per-entry target like `-cmp`) and the matcher's spelling / polarity. Not yet designed.
- **Smart-case matching (`--smart-case`) - [DISCUSS].** The rg / fd convention: an all-lowercase
  pattern matches case-insensitively, a pattern with any uppercase matches case-sensitively.
  Already referenced as an rg-flavor default (the `xfd`-drop and flavor-table notes below) but
  never defined as its own work item. Build `--smart-case` + an off switch, with the per-style
  default (on for rg, off for find / xff). **Sequence this BEFORE the flavor feature-map help
  row** - that table stays incomplete until smart-case is a real facet. **Open:** interaction
  with `--exact` and the FS case-fold probe, and whether it applies uniformly to `-name` globs,
  `-regex`, and `-grep`.
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
- **Sharded-file support (#84) - TBD, needs a design pass.** Treat a set of shard files
  (`data-00000-of-00010`, split `foo.tar.001` parts, ...) as one logical entry for matching /
  listing / content actions. **No design yet:** which shard schemes are in scope, whether it is
  a traversal-time grouping or a virtual `vfs` view (cf. `--archive`), and how it interacts with
  `-grep` / `-size` / `--count` over the reassembled whole. Decide the shape before building.
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
  - **FOLLOW-UP `--ignore-vcs` / `--no-ignore-vcs` (#132).** The rg-style toggle for VCS-provided
    ignore _files_ (a different axis from `--skip-vcs`'s dirs): `--no-ignore-vcs` drops the VCS
    ignore-file layer (`.gitignore` + `.git/info/exclude` + global git excludes; later `.hgignore`)
    while keeping `.ignore` / `.xffignore`. Needs a precedence spec before building:
    `--no-ignore`/`-u` (all off) > `--no-ignore-vcs` (VCS ignore files off) > `-g`/`--gitignore`.
    Low urgency: today the only VCS ignore file is `.gitignore`, so it is nearly `--gitignore=off`;
    it earns its keep once xff reads non-git VCS ignore files.
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

- **Archive diving (#83, `--archive`): use libarchive - decided 2026-07-06.** Descend into archives
  and match/list their entries as virtual paths (`foo.tar.gz/inner/file.txt`) via a read-only
  `vfs::FileSystem` backend, so the whole predicate/action set (incl. `-grep` on entry content)
  works unchanged. Engine = **libarchive** via its BCR module
  (`bazel_dep(name = "libarchive", version = "3.8.1.bcr.2")`) - a clean first-class dep (no
  vendoring / rules_foreign_cc), less code than hand-rolling, covers tar/zip/cpio/ar/iso + the
  gz/bz2/xz/zstd/lz4 filters behind one streaming API. Detect by extension + magic under `--archive`.
  - **Two build variants planned:** _minimal_ (tar + gz + bz2; disable xz/zstd/lz4/mbedtls at the
    libarchive build config) and _extended_ (add xz/zstd/zip/...). The license/NOTICE footprint
    scales with the enabled codec set.
  - **NOTICE obligations (all permissive; must be maintained).** libarchive's closure adds bzip2,
    lz4, xz, zlib, zstd, mbedtls. Net-new license types vs our Apache-2.0 / BSD-3-Clause baseline:
    **BSD-2-Clause** (libarchive, lz4), **Zlib**, **bzip2-1.0.6**, **0BSD** (xz - no notice needed).
    Two are dual-licensed: **pin zstd -> BSD-3-Clause** and **mbedtls -> Apache-2.0** (never their
    GPL arms), and link lz4's **library** (BSD-2), not its GPL-2.0 CLI. With those arms pinned there
    is no copyleft. Ship a third-party-notices file carrying each permissive notice; extend it as
    the codec set grows (minimal variant needs only BSD-2 + Zlib + bzip2).
  - **Scope: recurse into any archive found, not roots-only (decided 2026-07-09).** `--archive` is
    opt-in (default off = archives are plain files everywhere; `find .` unchanged). When on, an
    archive is transparently a directory WHEREVER it appears - a named root (`xff --archive foo.tgz`)
    AND every archive met during a walk (`xff --archive . -grep TODO` searches inside all of them).
    One uniform rule (archive == directory), not special-cased roots (roots-only can't do the walk
    case, which is the point). Entry path = the archive path as a directory prefix
    (`foo.tgz/dir/file.txt`; globs / `{relpath}` compose). Nested archives recurse with a DEPTH CAP;
    a size/depth cost guard is a follow-up knob (opt-in, so the cost is the user's choice). The
    archive VFS is READ-ONLY: `-delete` / `-exec` / `-execdir` on an archive entry is a clean error,
    never a silent no-op (`-exec` extract-to-temp deferred). Encrypted archives: `-encrypted`
    detection only, no `--password` decryption.

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
    both lean and full). **Open detail:** what `--archive` does in a full build before real diving
    exists (avoid a silent no-op; "not yet implemented" is distinct from the minimal "not built in").

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
