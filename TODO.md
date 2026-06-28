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

### find / xff features (roadmap tail)

The standard find predicate surface is complete (the access predicates
`-readable` / `-writable` / `-executable`, `-inum` / `-samefile`, symbolic `-perm`
modes, `-lname` / `-ilname`, and `-fstype`; all in the CHANGELOG and covered by the
engine unit test), as is the reusable markdown-table-alignment skill (#66). What
remains below is the design-forked / larger work.

- **Parallel traversal + `--jobs` + deterministic `--sort`** (#43). The big one;
  needs a design pass (work-stealing vs per-root pool; ordering guarantees).
- **Exit-code model refinement + `--skip-unsupported` + impossible-task-fail**
  (#44). Shipped: (a) match-sensitive exit -- the default stays find semantics
  (0 ran / 2 error, match status never affects exit), while `--quiet` (suppress
  output, exit by match) and `--exit-match` (keep output, exit by match) make
  "1 = no match" reachable; an error still outranks match status (exit 2). (b)
  impossible-task-fail -- a predicate that cannot be evaluated correctly on an
  entry's FS (e.g. `-Btime` where birth time is unrecorded) is a hard error
  (exit 2), reported once; `--skip-unsupported` downgrades it to a warning + skip.
  Still open: the grep-compat `-q` short alias of `--quiet` (single-dash whole-run
  flag tensions with the dash-count convention -- decide before adding); and
  extending impossible-task detection beyond birth time (only `-Btime`/`-Bmin`/
  X=B `-newerXY` flag it today; a Y=B reference with no btime stays a silent
  no-match, and other FS-capability gaps are not yet modelled).
- **`--exact` FS-aware matching + `--path-encoding` output** (#45).
- **`--feature=NAME` / `--feature=no-NAME` capability gates** (config phase D3,
  #73).
- **Grow `xff/datetime` into a parse+format lib** (#70): named formats, field
  modifiers, and the `--time-format` / `--timezone` global flags have shipped, as
  have the last deferred pieces -- the `--tz` short alias and fixed-offset zone
  specs (`+05:30` / `-0800` / `+01`). Nothing outstanding here.
- **`--mode=NAME` + argv[0] mode mechanism** (#54). The `--modern` umbrella flag
  stays deferred; ship per-feature gates first.
- **Full help system** (grow the `--help` overview shipped in #171). Today `--help`
  is a flat one-page summary; turn it into detailed, topic-addressable help:
  - Explain everything in depth, including the **whole config system** (layering /
    precedence, `.xffrc` discovery, `--config` / `--no-config` / `--xffrc` /
    `--explain`) and **especially flavor selection by alias / sym-linking** (the
    `argv[0]` dispatch: invoked as `find` -> strict find, as `xff` -> modern;
    explicit `--config` overrides).
  - **Topic help**: `xff help <topic>` for sub-commands, global flags, and matchers
    / primaries -- e.g. `xff help -regex`, `xff help --config`, `xff help <subcommand>`.
    Foundation landed: every descriptor now carries a one-line `summary` and
    `registry::All()` enumerates them, so per-primary topic help can read the
    registry directly; global-flag and config topics still need wiring.
  - **Generated reference docs from the same registry SOT.** Now that
    `registry::All()` enumerates every primary with a `summary`, drive documentation
    off it instead of hand-maintaining parallel copies:
    - **Man page on demand**: emit roff/troff (so `man -l -` / a packaged `man xff`
      works), e.g. `xff --man` or `xff help --man`, generated at runtime from the
      registry + the global-flag table + the config docs.
    - **Integrated Markdown documentation build**: emit a `.md` reference of all
      primaries / flags from the same source (an integrated subcommand and/or a build
      target; a separate external generator alongside the man-page builder is fine if
      need be). Wire it into CI so the committed docs cannot drift from the vocabulary.
- **Extended logical operators**: shipped. `-xor` / `-nand` / `-nor` / `-xnor` are
  xff extensions (find has only `-a`/`-and`, `-o`/`-or`, `-not`/`!`), with the
  conventional precedence `NOT > AND/-nand > XOR/-xnor > OR/-nor`; the strict find
  style rejects them. (`-xor` matches exactly one side; the rest are the negations
  of and/or/xor.)
