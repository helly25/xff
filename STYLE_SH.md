# Shell Style

The shell style for helly25 repositories. Follow the
[Google Shell Style Guide](https://google.github.io/styleguide/shellguide.html) unless this document
or the repository's automated tooling says otherwise.

## Tooling

- Use Bash and start executable scripts with `#!/usr/bin/env bash`, followed by the licence header.
- Run `shfmt` rather than hand-formatting. This repository uses `shfmt -bn -ci -i=2 -w`; its output
  wins where it differs from Google's examples.
- Run `shellcheck`. Fix findings instead of disabling them when practical. A necessary suppression
  is placed immediately above the affected source or command and explains why. The bashtest source
  line may use the repository-wide `SC1090`, `SC1091`, and `SC2154` suppression because Bazel
  supplies both the runfile path and bashtest's exported variables at runtime.
- Scripts use `set -euo pipefail`. Quote expansions unless intentional splitting or globbing is the
  point, and declare function variables with `local`.

The `shfmt` and `shellcheck` versions and arguments are pinned in
[`.pre-commit-config.yaml`](.pre-commit-config.yaml). Run `pre-commit run --files FILE...` before
committing shell changes.

## Functions return values; they do not mutate callers

A function does not communicate a result by changing a caller variable, a global counter, the
working directory, shell options, traps, or other hidden caller state. Return text on standard
output and capture it with command substitution:

```sh
_tree() {
  local root
  root="$(mktemp -d "${BASHTEST_TMPDIR}/tree.XXXXXX")"
  mkdir -p "${root}/src"
  echo "${root}"
}

root="$(_tree)"
```

Do not use `_tree root` with `printf -v`, `eval`, or a global sequence counter. Command substitution
runs the function in a subshell, so hidden state changes would disappear and make the interface
misleading.

An operation whose purpose is an external effect may perform that named effect: a fixture builder
creates files, a runner starts a command, and a release script updates the files it is explicitly
given. Keep the effect local and explicit, and reserve standard output for the documented result.
Diagnostics go to standard error.

## Tests and temporary files

- Shell tests use `helly25_bashtest` and its expectation helpers.
- Put test-owned files below `${BASHTEST_TMPDIR}`. Bashtest removes that directory even when an
  expectation fails; do not add per-case cleanup traps or `rm -rf` calls.
- Allocate retained fixtures with `mktemp -d "${BASHTEST_TMPDIR}/name.XXXXXX"`. This is unique per
  call without caller state, and its random component does not leak a test name into output checks.
- Use `${TEST_TMPDIR}` only where a tool specifically requires Bazel's target-level test directory.
- Prefer bashtest expectations over hand-written `grep` checks. The
  [`check_bashtest_grep.sh`](tools/check_bashtest_grep.sh) pre-commit check enforces this for
  bashtests.

## Portability and repository conventions

- Target the Bash available on supported macOS and Linux CI runners; do not depend on a newer Bash
  merely because it is installed locally.
- Resolve Bazel runfiles through `${TEST_SRCDIR}` and `${TEST_WORKSPACE}`. If a fallback search is
  required for a direct invocation, keep it confined to the binary-resolution helper.
- Prefer `[[ ... ]]`, arithmetic `(( ... ))`, and arrays over legacy test syntax and stringly typed
  argument assembly.
- Use lower-case names for local variables and functions. Read-only environment/runfile inputs keep
  the upper-case names supplied by Bazel or the dependency.
