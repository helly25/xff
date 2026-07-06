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

- **Project `.xffrc` per-entry subtree scoping (deferred).**
  The cascade (config phase E2a) reads, for each search root, every `.xffrc` from
  the filesystem root down to the root's directory (ancestors), applied run-level.
  The design (design-config.md L41, L56-58) also wants gitignore-style _subtree_
  scoping: a `.xffrc` in a directory _below_ a root should apply only to that
  subtree -- which means config resolution would vary per directory during the
  walk, an architectural change (per-entry layering on the traversal hot path).
  Deferred until a real need appears; the ancestor cascade already covers the
  common "repo + parents" case.

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

### find / xff features (roadmap tail)

The standard find predicate surface is complete (the access predicates
`-readable` / `-writable` / `-executable`, `-inum` / `-samefile`, symbolic `-perm`
modes, `-lname` / `-ilname`, and `-fstype`; all in the CHANGELOG and covered by the
engine unit test), as is the reusable markdown-table-alignment skill (#66). What
remains below is the design-forked / larger work.

- **Parallel traversal + `--jobs` + deterministic `--sort`** (#43). Built: a
  worker-pool walk (`ReadPool`, `absl::Mutex`; parallel `readdir`+`lstat` on workers,
  single-thread coordinator/visitor) with `--sort=none|dir|subtree|tree`
  (`absl::c_sort`), `-j N` / `--jobs=all`, mode-scoped defaults, unit-tested across
  worker counts plus a tsan CI cell. Remaining: a CLI-level bashtest exercising
  `--sort` / `-j` end to end, then close #43/#27.
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
  - **Explain the config system + flavor selection in depth** (in `--help`/topic
    help): config layering / precedence, `.xffrc` discovery, `--config` /
    `--no-config` / `--xffrc` / `--explain`, and **especially flavor selection by
    alias / sym-linking** (the `argv[0]` dispatch: invoked as `find` -> strict find,
    as `xff` -> modern; explicit `--config` overrides).
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
  - **`--help` readability + discoverability** (2026-07-04 feedback):
    - **Blank line before each section header** (`Traversal:`, `Matching:`, ...) in the
      `--help` overview, so the groups are visually separated.
    - **A full, detailed expression reference.** `--help=expressions` is still one-line
      summaries; add a detailed view - each primary's full description (arguments, style,
      cost, an example) - reachable via `--help=full` and/or `--help-full` (dump
      everything at full detail).
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
- **Line count as a first-class metric** (2026-07-04): a fully-featured per-text-file line
  count, beyond the shipped grep-style `-grep -c` / `--count` (#92). It should be a field in
  the vocabulary (`{lines}`, usable in `-printf` `%{lines}` / `--format` / per-file output),
  a `--summary` value (sum + a distribution / histogram of line counts across matches), and
  available to final / aggregate outputs - count lines everywhere counts and sizes already
  appear. (Binary files: no count, like the content detector.)
- **Per-file content hashes** (2026-07-04): compute a hash (md5 / sha\* / ...) per file,
  exposed as a field (`{hash}`, `{hash:sha256}`) and matchable, for dedup / change-detection
  / manifest output + `--summary` grouping. **Deferred pending the next `mbo` version**,
  which will provide the hashing (like `-diff` waits on mbo's diff); build against mbo's
  hashes when they land rather than vendoring our own.
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

- **Heavy/special libs are composable build-time extras (decided 2026-07-06).** libarchive (#83),
  pcre2 (#85), and any later special dependency are gated behind Bazel flags, not always compiled
  in: the default binary is a lean core (RE2 only, no archive), and an extended binary is composed
  from the same tree by enabling extras. Per extra: a `bazel_skylib` `bool_flag` (e.g.
  `//xff:archive`, `//xff:pcre`, default False) + a `config_setting` + `select()` so the extra's
  srcs/deps (`@libarchive`, `@pcre2`) link only when on, plus a `-DXFF_WITH_*` define so the backend
  registration `#ifdef`s in. A `.bazelrc` convenience config (`build:full --//xff:archive
--//xff:pcre`) composes them; CI builds both the lean and the full binary. The CLI reports which
  extras are compiled in (`--version` / help) and a disabled feature errors clearly ("not built in;
  rebuild with `--//xff:archive`"), never crashes. This is BUILD-time composition (what code/deps
  are in the binary), distinct from the #73 `--feature` RUNTIME gates. The third-party NOTICE is
  assembled from the enabled extras, so a lean build carries none of their notices.

- **PCRE2 backend (#85, `-regextype`): use pcre2 as a composable extra - decided 2026-07-06.** RE2
  (our engine) is linear-time and omits backreferences / lookaround / recursion; pcre2 is the Perl
  superset a `-regextype pcre`/`perl` grammar needs (RE2 already covers the POSIX-family grammars,
  which are all regular). **pcre2 is in the BCR**, upstream-maintained
  (`bazel_dep(name = "pcre2", version = "10.47")` - a stable release, not the 10.46-DEV snapshot); a
  clean dep, BSD-3-Clause (same family as re2 / googletest, so no new license type). Add a
  PCRE2-backed `regex::Matcher` behind the existing `xff/regex` abstraction, gated by the
  `//xff:pcre` extra above; keep **RE2 the default**, PCRE2 opt-in via `-regextype`, and set pcre2
  match / backtrack / depth limits (`pcre2_set_match_limit` etc.) so an adversarial pattern (ReDoS,
  which RE2 is immune to) cannot hang a walk.

- **Richer stats: histograms (#81) - design pinned 2026-07-06.** Histograms of "what the user
  sees": aggregate a metric grouped by a field and draw it as bars. `--summary` (the count+size
  group table) and `--histogram` are **independent, combinable terminal reductions** - a list of
  reduction specs, ONE walk feeds all of them, blocks render in declared order, and any reduction
  suppresses the per-match listing (like `--summary` today; an explicit `-print` / action brings it
  back). `--top=N`, `--summary-precision=N`, and `--human` apply to every block's numeric column.
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
