# xff — Test Plan

> Companion to [implementation-plan.md](implementation-plan.md). Project rule: **tests at every level; no one-shots** — any manual verification must be codified here or as a test before a PR is reviewable.
> Status: **Draft** · 2026-06-07

## Levels

### 1. `parser` — pure unit tests (no fixtures)
- Table-driven `argv[] → AST` (globals, roots, expression tree) and `argv[] → diagnostic` (message + source span).
- Cover: cluster parsing (`-Hg+x`), suffix-sign toggles (bare/`+`/`-`), `--long`/`--no-X`, `--` boundary, paths beginning with `-`/`+`, GNU vs BSD globals, `-exec … \;` / `… +` greedy consumption, operator precedence (`! > -a > -o`, `,`), arity errors.

### 2. find-compatibility conformance (Phase-1 gate)
- A fixture tree (varied names incl. dotfiles, sizes, mtimes, perms, symlinks, nested dirs).
- An **expression matrix** run through both real `find` and `xff`; assert identical output + exit code.
- Run on **both dialects**: Linux GNU-find and macOS BSD-find.

### 3. `engine` — integration
- Predicate/action correctness over fixture trees; `-exec`/`-delete`/`-prune` effects; `-xdev`, depth bounds.
- Parallel-traversal **determinism** under `--sort`; short-circuit evaluation order observable via side-effecting actions.

### 4. `regex`
- Per-engine conformance (RE2 vs PCRE2); literal-prefilter must not change match results (equivalence test).

### 5. `render` — golden tests
- One golden per format (plain/NUL/JSONL/CSV/aligned/markdown/tree).
- **Unicode-width alignment** (CJK/emoji/combining marks line up); **non-UTF-8 path** handling in JSON (escape policy).

### 6. `config`
- Cascade precedence (CLI > local dir > user > default); provenance (`unset` ≠ `off`); promotion-to-modern defaults.

## CI
- `bazel test //...` on macOS + Linux for every PR; buildifier lint.
