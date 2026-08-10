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

- **INVESTIGATE (GATES 1.0.0): `--flag:modifier` instead of `--flag=modifier value`.**
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
  - **Control surface: `--archive[=none|roots|all]` + `-z` (RATIFIED 2026-08-05; supersedes the
    2026-07-09 "always recurse" framing).** Diving into a NAMED ARCHIVE ROOT and diving into archives
    MET MID-WALK are two separately-wanted behaviors, so they are one ordered enum
    (`none` subset `roots` subset `all`) rather than a single boolean:
    - `none` = find-compat, an archive is one plain file; `roots` = dive only when a given root path
      is itself an archive; `all` = dive archives anywhere (roots plus ones discovered mid-walk).
    - Bare `--archive` = `all`. Short flag `-z` carries chmod-style suffix-signs, the same family as
      the `-g` gitignore trio: `-z-` = none, `-z` = roots, `-z+` = all.
    - **Flavor defaults:** `find` -> `none` (drop-in fidelity); every xff-family flavor (xff / xfd /
      rg) -> `roots`, because pointing xff AT an archive strongly implies "look inside", while
      silently descending every archive in a tree is a cost the user should opt into.
    - **Slice SHIPPED (2026-08-10): the spelling library + both flags.**
      `@xff_extras_api//:member_path_cc` (`xff/archive/member_path.h`) implements the ratified rules -
      `JoinMemberPath` is plain concatenation, `SplitMemberPath` cuts at the FIRST separator and keeps
      the remainder verbatim, an empty separator never matches, and `--archive-prefix=URI` parses only
      the URI form so the two spellings cannot silently interchange. It lives in the shared API module
      because both sides need it (the extra renders, the core parses a member path handed back) and an
      extra must not depend on the core. `--archive-separator=STRING` and `--archive-prefix=[URI|STRING]`
      are registered globals (help + `XFF.md` regenerated), gated on the archive extra like
      `--archive`. STILL TO COME with the VFS backend: actually rendering walk output through them,
      and the reserved `{relpath}`-style interaction. The URI scheme is the generic `archive://` for
      now - per-format (`tar:` / `zip:`) vs `jar:file:` wrapping is the one open sub-detail below.
    - **Member path spelling: a FLAG, because there is no single convention (decided 2026-08-10).**
      The prior docs disagreed (`docs/design.md` said the JAR-style `pkg.tar!foo/bar`, an older scope
      note said a plain directory prefix `foo.tgz/dir/file.txt`) - and neither is "right", because
      the ecosystem genuinely uses several: `!` (JAR / Java URLs), `#` (fragment style), and URI
      forms (`tar://…`). So the SEPARATOR is a presentation choice under user control, not something
      to hard-code:
      TWO orthogonal knobs, not one:
      - **`--archive-separator=STRING` (default `!`)** - the string placed between container and
        member. Any string is accepted, not a fixed menu: `!` (JAR / Java URLs), `#` (fragment
        style), and the multi-character `!/` or `#/` that other systems spell it as must all just
        work, so xff can produce paths those systems accept. A plain `/` is possible too ("it just
        looks like a directory", so globs and `{relpath}` compose with no new rules) but is lossy - a
        real directory named `x.tar` becomes indistinguishable from an archive - so it can never be
        the default.
      - **`--archive-prefix=[URI|STRING]` (default empty)** - whether the whole path is rendered as a
        URI (`tar:///abs/path/a.tar!/inner/x`) instead of a bare path. The point is INTEROP: a URI
        can be handed to other tools that understand archive URLs, which a bare path cannot. Open
        sub-detail: whether the scheme is per-format (`tar:` / `zip:`) or one generic scheme, and
        whether `file:` wraps the container path as Java's `jar:file:/…!/…` does.
      - Both flags apply to RENDERING and to PARSING a member path handed back in (a `-cmp` /
        `{def.X}` target), so a path xff printed always round-trips through xff.
      - The find flavor is unaffected: with `--archive=none` no member path is ever produced.
    - **Rendering is plain concatenation, so an ABSOLUTE stored member keeps its own leading slash.**
      `container + separator + member`, with the member reproduced exactly as the archive stored it.
      xff never adds or removes a slash - an archive may legitimately contain absolute member paths,
      and those must remain visible (that is the Zip-Slip red flag). It follows mechanically:
      - separator `!` -> `a.tgz!relative` vs `a.tgz!/rooted`
      - separator `!/` -> `a.tgz!/relative` vs `a.tgz!//rooted` (the doubled slash is correct, and
        the reason the plain `!` or `#` is the better default: with `!/` an absolute member reads as
        an odd `!//`)
        Parsing splits at the first occurrence of the configured separator and takes the remainder
        verbatim, so an absolute member round-trips instead of being normalized away.
    - **`--archive-prefix` is string-valued, with `URI` as the one keyword (refined 2026-08-11).**
      Empty means no prefix; `URI` renders a WELL-FORMED URI; anything else is used literally (e.g.
      `--archive-prefix=vfs:`), the same freedom the separator has. Two corrections came out of review:
      - `archive://a.tgz!x` was WRONG. `//` introduces the URI authority, so a relative container would
        parse as a HOST NAME. Absolute containers now render `archive:///abs/a.tar!x` (empty authority,
        as `file:///...` does) and relative ones the opaque `archive:a.tgz!x`. The original test only
        covered the absolute case, which is exactly why the bug hid - both forms are pinned now.
      - There is no `none` value: with a string-valued prefix it would be indistinguishable from a
        literal prefix spelled `none`. Keywords are ALL CAPS (`URI`), matching RE2 / PCRE2 / GLOB.
    - **OPEN: per-format schemes, and PHAR support (raised 2026-08-11).** PHP's phar has its own
      established URL form, `phar:///path/to/a.phar/inner/x` - a per-format scheme AND a plain `/`
      separator with no marker at all. That is real evidence AGAINST the generic `archive:` scheme and
      for per-format ones (`tar:` / `zip:` / `phar:`), the open sub-detail above; if per-format wins, the
      scheme becomes a property of the detected container format rather than one constant.
      Supporting phar itself is a separate slice: libarchive does NOT read phar (stub + manifest +
      optional per-entry compression + signature), so it needs its own reader behind the same
      `archive_reader` shape - which is what the extras architecture is for, and the member-path
      spelling is format-agnostic so nothing there changes. Check the existing phar work for a reusable
      manifest parser before writing one.
    - **OPEN (design sketched 2026-08-11): `--archive-separator=AUTO+<fallback>`, a per-format
      separator.** Once the scheme can be per-format, the SEPARATOR has to follow the format too, since
      phar's convention is a bare `/` with no marker while JAR-style URLs use `!`. `AUTO` alone cannot
      decide between `!` and `#` for the formats that have no convention, so it needs a fallback,
      spelled compactly as one value:
      - `AUTO+!` - derive from the container format, and use `!` where the format dictates nothing.
      - Bare `AUTO` means `AUTO+!` (the current default fallback), so the short form stays useful.
      - ALL CAPS because that is now the keyword convention (`URI`); any other string stays a literal
        separator, so a literal `auto` is still expressible and unambiguous.
      - The `+` reads as "with fallback". Note it is a mild overload: `+` elsewhere in xff is the
        chmod-style "more/on" suffix (`-z+`, `-g+`, `-s+`). Alternatives considered and rejected:
        `AUTO:!` (`:` already means sub-separator inside a value) and `AUTO!` (unparseable - `!` is
        itself a legal separator, so where does the keyword end?).
      - Known per-format mapping to start from: phar -> `/` (PHP's `phar:///a.phar/inner/x`), Java
        jar/war/ear -> `!` (JAR URLs), everything else -> the fallback.
      - **A bare `/` is NOT lossy - correcting an earlier note here.** A walk is never ambiguous: it
        meets `a.phar` as a real FILE, sniffs it and descends, and one path cannot be both a file and a
        directory. The only inconvenience is that a `/` boundary is not locatable by string inspection
        alone, and even that is solvable WITHOUT the filesystem: scanning for a known container
        EXTENSION works, which is how PHP resolves `phar:///path/a.phar/inner` and why `.phar/` is
        itself a split point. So splitting stays offline-capable and a printed path round-trips.
        `SplitMemberPath`'s oracle overload covers all three (walk knowledge, stat + sniff, or a purely
        lexical extension test); the string-only overload refuses an all-slash separator rather than
        cutting at the leading slash. Note the marker separators are heuristics too: a directory really
        can be named `foo!`, so `!/` and `#/` can occur in a real path - they just fail to be archives,
        so a walk finds nothing and the marker only ever served as a convenient split point.
    - **Container identity is dual:** the archive keeps its real-FS identity (real `-type f`,
      deletable / actionable) AND parents its members; this falls out of the existing VFS
      source-tagging (container = real fs, members = archive member).
    - **Nesting has its own cap `--archive-depth=1`** (decompression-bomb risk), independent of
      `-maxdepth`; members still count toward `-maxdepth` normally.
    - **Detection** = libarchive content sniff, but in `all` mode the sniff is gated by a
      known-archive extension / magic peek so a whole tree is not sniffed byte-wise;
      `--archive-any` forces sniff-everything (expensive, opt-in).
    - Raw-compressed single files (`.gz` / `.xz` / `.zst` / `.bz2`) are one-member archives whose
      member is the inner name.
    - The archive VFS is READ-ONLY: `-delete` / `-exec` / `-execdir` on an archive member is a clean
      error, never a silent no-op (`-exec` extract-to-temp deferred). Encrypted archives get
      `-encrypted` detection only, no `--password` decryption.
    - Read-only member semantics, the `container!member` representation with the `!/` Zip-Slip red
      flag, uncompressed logical size, and the streaming / bomb limits were already specified in
      `docs/design.md` "Virtual entries".

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
    `s///`/`m//` stay as-is (regex args are delimiter-hostile); only reducers use function notation. - **SHIPPED span-diagram help (#143).** The `--help=fields` topic (and the doc*renderer FIELDS
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
- **Fuzzy finding + near-duplicate detection** (design open). Two distinct capabilities that share the
  "approximate match" theme; split them, do not conflate:
  1. **Fuzzy name/path matching** - an fzf/fd-style approximate match over the path or basename, as its
     own primary (single-dash, e.g. `-fuzzy PATTERN` / `-ifuzzy`) rather than overloading `-name`.
     Decide the algorithm: subsequence match (fzf-style: characters of PATTERN appear in order, with a
     rank score) vs bounded edit distance (Levenshtein <= k). Ranking implies output ordering, so it
     ties into `--sort` (a `--sort=score` mode) and a possible `--top=N`; a bare boolean predicate can
     also just gate at a score threshold. xff-flavor only (kXff-gated); find flavor rejects it.
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
- **The MSan finding: DIAGNOSED as a false positive, ignore-listed (opened 2026-08-10; diagnosed
  2026-08-10).** The symbolized frames (once `MSAN_SYMBOLIZER_PATH` was set) show every test failing
  at the same single site, and identify it as an instrumentation gap rather than a bug:

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
  - **Coverage gap, deliberate:** the three `xff_extras_api` tests do NOT route through the wrappers,
    so they carry no suppression file. That module is built both as `//xff_extras_api:...` (it is not
    in `.bazelignore`) and as its own `@xff_extras_api`, and in the latter a `//xff:cc.bzl` label does
    not exist - loading it would also invert the "an extra never depends on the core" rule. The
    `no-raw-rules-cc-load` hook exempts that directory for the same reason. If those tests need the
    suppression, the file has to be hosted by the shared API module itself.
  - **Format caveat, still to be proven in CI:** MSan's runtime suppressions understand only
    `interceptor_via_fun` / `interceptor_via_lib` (reports raised through an intercepted libc call).
    This report comes from inline `std::string` code, so it is not certain the entry matches. If the
    `msan` cell still reports after this, the remaining options are patching abseil (a declared,
    hermetic input) or fixing the cause below.
  - **BLOCKED, and now proven so (2026-08-10).** The swap chain was fixed step by step: `%workspace%`
    is not expanded in a copt value (so it silently did nothing), an absolute `-isystem` is rejected
    as outside the execroot, and `-cxx-isystem` from a generated bazelrc finally worked - the headers
    do come from `.msan-libcxx`, and the false positive DISAPPEARS. But the compile then fails,
    because two libc++ copies end up on the search path:

    ```
    .msan-libcxx/include/c++/v1/__functional/hash.h:40:8: error: reference to unresolved using declaration
       std::memcpy(std::addressof(__r), __p, sizeof(__r));
    .msan-libcxx/include/c++/v1/cstring:82:1: note: using declaration annotated with 'using_if_exists' here
    .msan-libcxx/include/c++/v1/cwchar:136:9: error: target of using declaration conflicts with declaration already in scope
    ```

    `bazel`'s `cc_toolchain` passes its OWN libc++ include dir explicitly, and `-nostdinc++` cannot
    remove what the toolchain adds by hand, so `#include_next` resolves across two different libc++
    trees. A hand-built stdlib therefore cannot be swapped in under `toolchains_llvm` at all.

  - **Conclusion: MSan is parked.** Both routes are closed - the stdlib cannot be swapped (above), and
    the resulting false positive cannot be suppressed at runtime (interceptor-only format). The only
    real fix is a toolchain that builds its runtimes in-bazel under a sanitizer config, i.e.
    hermeticbuild/hermetic-llvm and its `//config:msan_enabled` runtimes. Until then the `msan` cell
    stays report-only (`continue-on-error`) and must not be hard-gated. Everything the attempt DID
    leave behind is worth keeping: the runtime-suppression plumbing, the `msan` tag, the
    `no-raw-rules-cc-load` enforcement, and `MSAN_SYMBOLIZER_PATH` in the run-under wrapper.

- **INVESTIGATE: the `clang-tidy` CI cell is ~5x slower than helly25/mbo's (opened 2026-08-10).**
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
  - **DECIDED 2026-08-09: build it, in CI.** The instrumented libc++ is not a blocker, just work:
    `tools/build_msan_libcxx.sh` builds libc++ / libc++abi / libunwind from the LLVM source release
    matching `bazelmod/llvm.MODULE.bazel`'s `llvm_version`, with `-DLLVM_USE_SANITIZER=MemoryWithOrigins`
    and the hermetic clang as the compiler, into `.msan-libcxx/` (gitignored). `--config=msan` swaps it
    in via `-nostdinc++ -isystem .../include/c++/v1` + `-nostdlib++ -L.../lib -Wl,-rpath,...`; every
    other dep is built from source under bazel so `-fsanitize=memory` instruments it for free.
  - **CI:** one `msan` cell (ubuntu only) mirroring `tsan`, with the instrumented libc++ cached on the
    LLVM version + the script hash (the script stamps its prefix and reuses a matching one). It starts
    `continue-on-error` while the first findings are triaged - the same introduction path the
    `clang-tidy` cell took - then becomes a hard gate in `done`'s `needs`.
