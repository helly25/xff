# Agent rules - xff

Conventions for AI agents and contributors working on this repository.
Companion to [`docs/design.md`](docs/design.md) (decisions) and
[`docs/implementation-plan.md`](docs/implementation-plan.md) (build & sequencing).
The full **C++ coding style** is [`STYLE_CPP.md`](STYLE_CPP.md); project-level rules are
[`RULES.md`](RULES.md); the contribution flow is [`CONTRIBUTING.md`](CONTRIBUTING.md). The
GoogleTest section below is the quick reference; `STYLE_CPP.md` is canonical.

Build & test: `bazel test //...` · sanitizers: `bazel test //... --config=clang --config=asan`
(also `--config=tsan`, and `--config=msan` on Linux).
Toolchain: clang-22 minimum (hermetic LLVM under `--config=clang`).

## Writing tests (GoogleTest)

1. **Always `TEST_F` with a fixture; never bare `TEST(...)`** - even when the
   harness is a one-line `struct FooTest : ::testing::Test {};`.
2. **The fixture is a `struct`** (not a `class`) inheriting from
   `::testing::Test` (or a friend such as `::testing::TestWithParam<T>`).
3. **Prefer `EXPECT_THAT` / `ASSERT_THAT` (gmock matchers) almost exclusively**
   over `EXPECT_EQ` / `ASSERT_EQ`.
4. **Multi-line text: `mbo::testing::EqualsText`, not `EXPECT_EQ`.** For a
   multi-line string use `EXPECT_THAT(actual, EqualsText(golden))` (unified diff,
   line by line). Write the golden as a `DropIndent`-filtered indented raw string,
   not concatenated `"...\n"` literals (which `clang-format` shoves against the
   `EqualsText(` paren): `WithDropIndent(EqualsText(R"out(` ... `)out"))`.
   `WithDropIndent` de-indents the expected text only; `DropIndentAndSplit` yields
   the lines as a vector. Caveat: a raw-string golden cannot carry significant
   trailing whitespace (the trim-trailing-whitespace hook strips it) - use the
   literal form there. `STYLE_CPP.md` is canonical. There is no `EXPECT_EQ` fallback;
   use `EqualsText` for text comparisons.
5. **Typed and parameterized tests supply names from the types/values** (name
   generators for `TYPED_TEST_SUITE` / `INSTANTIATE_TEST_SUITE_P`), so the
   output never shows numbered tests (`Suite/0`, `Suite/1`).
6. **Test `absl::Status` / `absl::StatusOr<T>` with status matchers - never raw
   `.ok()`.** Raw `EXPECT_TRUE(s.ok())` / `EXPECT_FALSE(s.ok())` throws away the
   code and message on failure. Use `mbo::testing`
   (`@helly25_mbo//mbo/testing:status_cc`):
   - `EXPECT_THAT(s, IsOk())`
   - `EXPECT_THAT(s, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("…")))`
   - `EXPECT_THAT(so, IsOkAndHolds(Eq(42)))`
   - `ASSERT_OK_AND_ASSIGN(const auto value, MakeThing());` to unwrap a `StatusOr`.

   mbo's set is the helly25-canonical superset: it works on both `Status` and
   `StatusOr`, and adds payload matchers (`StatusHasPayload`) plus the
   `EXPECT_OK` / `ASSERT_OK` / `ASSERT_OK_AND_ASSIGN` macros over abseil's
   `absl_testing`.

## Markdown

Keep GitHub-flavored Markdown tables **vertically aligned** (the `|` pipes line
up, every column padded to its widest cell, honoring the `:--` / `--:` / `:-:`
alignment markers). Do not hand-align them: run the formatter
[`tools/align_markdown_tables.py`](tools/align_markdown_tables.py) (`FILE...`, or
`--check` to only report). It is enforced by the `align-markdown-tables`
pre-commit hook, so a misaligned table fails CI; let the tool do the spacing.

## Self-documenting features (the registry is the single source of truth)

The expression vocabulary (`xff/registry`) and the global options
(`xff/cli/globals.cc`) are the single sources of truth from which `--help`,
`--help=TOPIC`, the `--help=list` index, the `--man` roff page, and the
`--markdown` reference are all generated. So **every feature add or change carries
its self-documentation in the same change** - this is part of "done", never a
follow-up:

- a new / changed **primary** -> its `registry::Descriptor.summary` (a non-empty
  one-line synopsis; `registry_test` enforces presence + shape);
- a new / changed **global flag** -> its `cli::GlobalFlag` entry in `globals.cc`
  (`globals_test` enforces it; set `alias` / `display` for short or alternate forms);
- a new / changed **cookbook recipe** (`cli/help.cc` `RenderCookbook`) -> a matching
  execution case in `//xff/examples:cookbook_test`; its guard case fails CI if a recipe ships
  without one, so the user-facing examples are run, not just rendered;
- a new **environment variable** the code reads -> a row in `cli/help_build.cc`'s `kVars` table
  (`--help=environment`), enforced by the `check-env-documented` pre-commit hook, which also fails on
  a documented name nothing reads;
- the hand-maintained `kHelpText` usage page in `cli/main.cc`;
- any prose docs the change affects (`docs/design-*.md`, `TODO.md`).

The generated `--help` / `--man` / `--markdown` then stay complete by construction.

### Markup in help prose

Help text is authored in a small inline markup (`ParseInline`), and the backends render it per
target: the plain backend keeps the backticks, roff bolds the span, Markdown makes it a code span.
So **write concrete literals as `` `code` ``** in `details`, in `ValueDoc.meaning`, and in topic
bodies: option values (`` `auto` ``, `` `all` ``), flag names (`` `--no-pager` ``), primaries
(`` `-exec` ``), and literal tokens (`` `*.{cc,h}` ``). A reader should be able to tell a value from
a word without parsing the sentence.

Two limits, both about noise:

- **`summary` stays plain.** Summaries appear in dense list views (`--help`, `--help=list`), where
  literal backticks on every row cost more than they explain. The exception is a literal that is
  unreadable bare, such as punctuation (`` `!` ``).
- **Never mark a word used in its English sense**, even when a value shares its spelling: "all
  archive features", "treats the duplicate as an error", "a git working tree", and "`ls`'s own
  colours" (the tool, not the `ls` value) all stay plain. This is why the sweep is done by hand -
  mechanical backticking gets exactly these wrong.

## CLI conventions

- **Flag scope by dash count.** `--flag` is a whole-run global (a config / output /
  traversal modifier); a single-dash `-flag` is an expression primary (a per-entry
  test or action). Grep / GNU single-dash _globals_ (`-h`, `-help`, `-version`,
  `-q`) are deliberate special-cased compatibility aliases of their `--` form, not
  new primaries.
- **`--` globals are position-independent.** Because every primary/operator is
  single-dash, a `--flag` is unambiguous anywhere, so the parser hoists it out of any
  primary/operator position - before the roots, among them, or in the expression
  (including the tail: `xff . -type f --summary=ext`). It is NOT hoisted out of a
  primary's argument run (an `-exec` command's args, a `-printf` format), where a
  `--flag` is a literal argument to that primary / the child command; a bare `--`
  ends option parsing and disables hoisting. Single-dash globals stay leading-only
  (they are ambiguous with primaries). The parser handles this in ExprParser's
  `SkipGlobals()` + the roots loop.
- **Flag-only; no subcommands.** xff is a single-purpose tool (like `fd` /
  `ripgrep`), so meta operations are flags (`--help`, `--man`, `--markdown`,
  `--explain`), never `git`-style subcommands; find and xff share one grammar,
  differing only in vocabulary. (Decided 2026-06-28.)
- **A boolean capability belongs to a FAMILY or to an existing flag's value set - not to a
  `--feature` namespace.** The `--feature=NAME` mechanism (#73) stays unbuilt: 22 boolean globals
  later, every one of them read better either as a named member of its family (the `--archive-*`
  set, which `--help=archive` then gathers) or as a value on a flag that already exists
  (`--case=smart`, not `--smart-case`). So when adding a boolean: name it into its family if it has
  one, fold it into a valued flag if that reads naturally, and only then consider a standalone
  boolean. The verdict is recorded in [`TODO.md`](TODO.md) under #73.
- **A feature whose SPELLING is not settled ships behind `--unstable=NAME`, not under a provisional
  flag name.** That is the one `--feature`-shaped mechanism xff adopts (design in
  [`TODO.md`](TODO.md) under #73): one repeatable, comma-separated list; an unknown name is a usage
  error; the gated flag's own help says it is unstable; graduation means DELETING the gate rather
  than keeping an alias. It gates spelling only - a destructive capability keeps its own explicit
  flag and is never armed by list.
