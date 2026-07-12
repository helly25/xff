<!-- SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com) -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# xff - eXtended File Find

`xff` is a `find(1)`-compatible file finder with modern extensions. It walks each starting path and acts on the entries matching an expression, exactly like `find`, then adds the conveniences you always wished `find` had: content and language search, structured output, per-run summaries and histograms, safe deletes, native hashing, and a shared `{field}` vocabulary that threads through `-printf`, `-exec`, and every renderer.

Anything `find` does, `xff` does the same way. Everything else is opt-in.

This `README.md` is a short overview. The complete, always-current reference lives in [XFF.md](./XFF.md) (generated from the binary; see Documentation).

---

## Core Highlights & Architectural Advantages

- **`find`-Compatible Core:** The standard primaries (`-name`, `-type`, `-size`, `-mtime`, `-regex`, `-exec`, `-prune`, ...), operators, and exit codes behave exactly as in GNU/BSD `find`. Invoked as `find`, it is strict `find` and nothing more.
- **Content & Metadata Matching:** `-grep` / `-content` search inside files, `-lang 'C*'` and `-mime 'image/*'` match by inferred language or media type, `-text` / `-binary` / `-eofnl` classify content, and native `-hash` primitives emit optimized checksum manifests.
- **Structured Layout Engines:** Stream matches natively as plain text, NUL-delimited, `JSONL`, `CSV` / `TSV`, an aligned console table, a visual tree, or a standard Markdown table, all calculated from one single filesystem walk.
- **Summaries & Histograms:** `--summary=ext` folds matches into counts and totals; `--histogram='ext:sum(lines)'` draws terminal bar charts using Unicode block characters. No external `awk | sort` pipeline overhead required.
- **Unified `{field}` Vocabulary:** The same named fields (`{relpath}`, `{size}`, `{lang}`, `{hash}`, `{capture.NAME}`, ...) drive `-printf`, `-exec`, `--format`, and `--summary`, complete with powerful `s///` regex rewrite and `m//` extraction qualifiers.
- **Safe by Default:** `-delete` implicitly forces `-depth` and strictly honors `--dry-run` / `--safe`. Configuration tiers loaded via an `--xffrc` file are sandboxed: they cannot execute dangerous directives (`-exec`, `-execdir`, `-ok`, `-capture`, or `-delete`) unless explicitly armed via a trusted CLI flag (`--allow-exec`).
- **Fast and Parallel:** `-j N` parallelizes the filesystem walk and concurrent `-exec` pools across available CPU worker cores; `--sort` delivers deterministic sibling ordering when requested.
- **Virtual Archive Inspection:** Native capability to descend into compressed archives (`--archive`) like `.tar`, `.zip`, `.zstd`, and `.lz4` as virtual paths, executing content matching transparently on uncompressed streams without extraction disk space overhead.

---

## Tool Feature Comparison Matrix

The matrix below maps the primary feature landscapes across legacy and modern single-purpose utilities to show what `xff` consolidates into a single execution pass.

| Feature / Capability                 | `find` | `fd` | `grep` | `ripgrep` (`rg`) | `shasum` | `xff`                                    |
| :----------------------------------- | :----: | :--: | :----: | :--------------: | :------: | :--------------------------------------- |
| **Multi-threaded Traversal**         |   -    |  ✓   |   -    |        ✓         |    -     | **✓ Native Pool (`-j`)**                 |
| **Implicit VCS/Gitignore Awareness** |   -    |  ✓   |   -    |        ✓         |    -     | **✓ Configurable (`-g`)**                |
| **Regex Content Search Engine**      |   -    |  -   |   ✓    |        ✓         |    -     | **✓ Native (`-rxc`/`-grep`)**            |
| **Inline Cryptographic Hashes**      |   -    |  -   |   -    |        -         |    ✓     | **✓ Native (`-hash`)**                   |
| **Virtual Archive Traversal**        |   -    |  -   |   -    |        -         |    -     | **✓ Native (`--archive`)**               |
| **Custom Layout Templating**         |  GNU   |  -   |   -    |        -         |    -     | **✓ Rich (`--template`)**                |
| **Fine-Grained Line-Ending Linting** |   -    |  -   |   -    |        -         |    -     | **✓ Native (`-eofnl`/`-eofcrlf`)**       |
| **Deterministic Reduction Tables**   |   -    |  -   |   -    |        -         |    -     | **✓ Native (`--summary`/`--histogram`)** |

`find`'s "Custom Layout Templating" is marked **GNU** because it is GNU find's `-printf`, a
GNU extension; POSIX and BSD/macOS `find` have no format primary (only `-print` / `-exec`).

---

## Flavor & Behavioral Shift Matrix

`xff` runs one unified grammar under three operational flavors. The flavor is selected automatically by the program binary name and can be explicitly overridden or layered using the `--config` flag (where the last specified style wins).

The table below illustrates how traditional shell workflows shift into optimized `xff` unified expressions.

| Target Intent / Use Case          | Legacy Command / Pipeline                  | The `xff` Unified Expression                                    | Architectural Advantage / Behavioral Shift                                                                                                  |
| :-------------------------------- | :----------------------------------------- | :-------------------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------ |
| **Strict Compliance**             | `find . -type f -name "*.cpp"`             | `find . -type f -name "*.cpp"` <br>_or_ `xff --config=find ...` | **Strict POSIX compatibility mode:** Turns off all modern extensions; modern flags become immediate usage errors.\*                         |
| **Modern Structural Search**      | `fd -e cc`                                 | `xff -regex '.*\.cc$'` <br>_or_ `xff --config=xff ...`          | **Evolved mode (Default):** Expands `find`'s grammar with modern extensions, enabling sorted output and human sizes (`--human=si`).         |
| **Clean Developer Grep**          | `fd -H -E ".git" \| xargs rg "TODO"`       | `xff --config=rg -grep "TODO"`                                  | **Opinionated Developer Mode:** Implicitly respects nested `.gitignore` files, skips hidden files, and uses smart-case matching logic.      |
| **High-Performance Verification** | `find . -type f -exec sha256sum {} \;`     | `xff -type f -hash=sha256`                                      | **Zero-Fork Speed:** Eliminates system process-spawning overhead. Reads files directly into the native read loop buffer to hash inline.     |
| **Missing Newline Code Linting**  | _Complex multi-line awk scripts or loops._ | `xff -text ! -eofnl -print`                                     | **Native Classification:** Instantly flags text files violating POSIX trailing newline rules without streaming lines to the shell.          |
| **Cross-OS Time Constraints**     | `find . -mmin -60`                         | `xff -mtime "-3 weeks 3 hours"`                                 | **Advanced Parsing:** Uses human-readable compound duration strings interpreted cleanly via explicit IANA `--timezone` modifiers.           |
| **Compressed Asset Auditing**     | `tar -ztf src.tar.gz \| grep "cfg"`        | `xff --archive -path "*src.tar.gz*cfg*"`                        | **Virtual File-tree Mapping:** Treats archives as virtual read-only directories, matching inner structures without manual disk extraction.  |
| **Isolated Variable Outputting**  | `find . -printf "%p,%s\n"`                 | `xff --format=csv --columns=path,size`                          | **Structured Sanitization:** Formats data cleanly into formal arrays with safe, native C-escape column handling (`--path-encoding=escape`). |

\* **Strict `find` mode is still xff's engine, not a wrapper around the OS `find`.** It keeps
find's vocabulary and turns the xff extensions into usage errors, but the implementation is one
fast, mostly platform-independent binary. The clearest divergence is regex: `-regex` / `-iregex`
default to **RE2** (linear-time, no catastrophic backtracking) and behave identically on Linux
and macOS - where GNU find instead defaults to its Emacs dialect and BSD/macOS find to BRE.
`-regextype` selects xff's uniform grammar set (RE2, EXACT, FNMATCH, GLOB, SHGLOB, plus PCRE2 in
a full build), never GNU's dialect names. Otherwise strict mode is find's documented behavior,
made uniform across platforms.

---

## Quick Start

`xff` builds with Bazel and runs on macOS and Linux.

```bash
# Build and run the stock binary.
bazel run //xff -- . -type f -name '*.md'

# Or build it once and put it on your PATH.
bazel build //xff
cp bazel-bin/xff/cli/xff /usr/local/bin/xff
```

---

## Examples

```bash
# Ten largest files (-printf builds any columnar line; the shell sorts).
xff . -type f -printf '%s\t%p\n' | sort -rn | head

# Disk use per file type (a --long global like --summary may sit at the end).
xff . -type f --summary=ext

# Delete stale temp files, safely (prints what -delete WOULD remove).
xff . -type f -name '*.tmp' -mtime +7 -delete --dry-run

# Search code content, filtered by language (path:lineno:text for every TODO).
xff src -lang 'C*' -grep 'TODO'

# Checksum manifest for a tree (like sha256sum: `DIGEST  PATH` per file).
xff . -type f -hash=sha256

# Recently changed files as machine rows (one JSON object per file, for jq).
xff . -type f -mtime -1 --format=jsonl
```

See the [XFF.md](./XFF.md) cookbook for more worked examples, including native per-author `git blame` line counts computed with no shell pipes.

---

## Documentation

The vocabulary and options are defined once inside the C++ binary (the engine registry acts as the single source of truth), ensuring that every documentation surface is automatically generated and cannot drift:

- **`XFF.md`**: The full comprehensive reference in Markdown. It is a verbatim dump of `xff --markdown`, regenerated by `xff-md-update.sh` and guarded by the `//xff/cli:xff_markdown_test` target, which fails CI if any code-to-docs drift occurs.
- **`xff --help`**: Renders the main utility usage page. Use `xff --help=TOPIC` to review specific sub-topics (`fields`, `printf`, `time`, `size`, `grammars`, `stats`, etc.), or `xff --help=full` to dump all help sections.
- **`xff --man`**: Renders the standard `roff` man page stream.

---

## Building & Dependencies

The default build provides a lean, dependency-light core. Heavier processing capabilities (such as the advanced `PCRE2` regex grammar or recursive archive diving) are decoupled as composable build-time extras that are disabled by default to keep the core binary small. The extended target links them all:

```bash
# The full binary, with every extra (PCRE2, archive diving, etc.).
bazel build --config=xff_full //xff/cli:xff_full
```

The `//xff` target alias follows your active workspace configuration automatically: it resolves to the lean binary by default, and switches to the full binary under `--config=xff_full`. The underlying targets remain explicit and configuration-stable: `//xff/cli:xff` is always lean, and `//xff/cli:xff_full` is always full.

> **Compile-Time Enforcement:** The CLI options for extras (e.g., `--regextype=PCRE2` or `--archive`) are always exposed on the interface. Attempting to invoke an extra feature in a lean build that did not compile it will yield an immediate, explicit error rather than a silent failure or fallback.

- **Requirements:** Bazel accompanied by a modern C++23 toolchain (`clang-22` or newer). A fully hermetic LLVM toolchain is available out-of-the-box via `--config=clang`.

---

## Contributing

See [CONTRIBUTING.md](./CONTRIBUTING.md), the LLM agent and contributor guidelines in [AGENTS.md](./AGENTS.md), and the core systems architecture style in [STYLE_CPP.md](./STYLE_CPP.md). In-depth design notes live under `docs/` and the active development roadmap can be found in `TODO.md`.

---

## License

Apache License 2.0. See `LICENSE` and `NOTICE` for details.
