<!-- SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com) -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# 0.1.0

First public release of `xff` (eXtended File Find): a `find(1)`-compatible file
finder with modern extensions. Anything `find` does, `xff` does the same way;
everything else is opt-in. Runs on macOS and Linux, built with Bazel.

## Find compatibility

- The standard primaries (`-name`, `-iname`, `-path`, `-type`, `-size`,
  `-empty`, the `-mtime` / `-atime` / `-ctime` / `-mmin` families, `-newerXY`,
  birthtime `-Btime` / `-Bmin`, `-uid` / `-gid`, `-user` / `-group`, `-perm`,
  `-regex` / `-iregex`, `-links`, `-inum`), the operators (`-a`, `-o`, `!`,
  `,`, parentheses), and the actions (`-print`, `-print0`, `-printf`, `-ls`,
  `-exec` / `-execdir` with `\;` and `+`, `-ok` / `-okdir`, `-prune`, `-quit`,
  `-delete`) behave as in GNU/BSD `find`.
- Traversal control: `-depth` (and post-order), `-maxdepth` / `-mindepth`,
  `-xdev`, the symlink modes `-H` / `-L` / `-P` with loop detection.
- GNU/BSD-compatible exit codes. Invoked as `find`, `xff` is strict `find` and
  rejects its own extensions.

## Search and matching

- Content matching: `-content` / `-icontent` (literal) and `-rxc` / `-irxc`
  (regex) search inside files; a ripgrep-style `path:lineno:text` output action.
- Regex backends selectable with `-regextype`: RE2 (default), PCRE2 (full
  build), and a shell-glob grammar (POSIX classes, brace expansion).
- Type inference: `-lang 'C*'` matches by GitHub-linguist language and
  `-mime 'image/*'` by media type.
- Content classification: `-text` (with `apple` / `posix` / `windows` line-ending
  flavors), `-binary`, `-eofnl` / `-eofcr` / `-eofcrlf`.
- `--smart-case` case handling.

## Output

- Streaming renderers from a single walk: plain, NUL-delimited, JSONL, CSV, TSV,
  an aligned table, a tree, and a Markdown table.
- One `{field}` vocabulary (`{relpath}`, `{size}`, `{lang}`, `{hash}`, `{lines}`,
  `{capture.NAME}`, `{env.NAME}`, `{def.NAME}`, ...) drives `-printf` /
  `-println`, `-exec`, `--format`, and the aggregations, with `s///` rewrite and
  `m//` extract qualifiers (the `m//` pipeline supports a terminal `join`
  reducer).
- `%{field}` escapes bridge `-printf` to the brace-field vocabulary.
- File-type color output; shared right-aligned, thousands-separated, human-size
  number formatting across `-ls` and the summaries.

## Aggregation and comparison

- `--summary=KEY` folds matches into an aligned count + size table (or JSONL
  rows), repeatable for independent tables.
- `--histogram=BUCKET[:MEASURE]` draws distribution bars.
- Line counting (`grep -c` style) and native file hashing (`-hash=sha256`,
  `{hash}` field, hex / base64) producing checksum manifests.
- `-diff` produces a git-style unified diff against a per-entry target; `-cmp`
  is the pure comparison matcher.

## Safety

- `-delete` implies `-depth` and honors `--dry-run` / `--safe`; `-ok` / `-okdir`
  confirm interactively.
- A configuration loaded from an `--xffrc` file cannot run `-exec` or `-delete`
  unless explicitly armed.

## Performance

- `-j N` parallelizes the walk and concurrent `-exec`.
- `--sort=none|dir|subtree|tree` gives deterministic ordering when wanted.

## Flavors

- One grammar runs under four flavors selected by program name or `--config`:
  `find` (strict), `xff` (conservative, find-evolved), and the opinionated
  `xfd` / `rg` (gitignore-aware, skip-hidden). Invoked as `find`, it is `find`.
- `--ignore-vcs` / `--no-ignore-vcs`, `--skip-vcs[=LIST]`, and gitignore /
  `.ignore` / `.xffignore` handling.
- `--explain` prints the resolved flavor/config table.

## Composable extras

- A lean default binary (`xff`) and a full binary (`xff_full`) that enables
  build-time extras (currently PCRE2) via Bazel flags, so the core stays small
  and heavy dependencies are opt-in. `//xff` resolves to whichever the build
  selects.

## Help and documentation

- `--help`, `--help=TOPIC`, `--help=list`, `--man` (roff), and `--markdown` are
  all generated from a single source of truth (the expression registry and the
  global-options table), so they stay complete by construction.
- The generated Markdown reference is committed as `XFF.md` and guarded against
  drift by a CI test.
