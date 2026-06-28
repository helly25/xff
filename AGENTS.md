# Agent rules - xff

Conventions for AI agents and contributors working on this repository.
Companion to [`docs/design.md`](docs/design.md) (decisions) and
[`docs/implementation-plan.md`](docs/implementation-plan.md) (build & sequencing).
The full **C++ coding style** is [`STYLE_CPP.md`](STYLE_CPP.md); project-level rules are
[`RULES.md`](RULES.md); the contribution flow is [`CONTRIBUTING.md`](CONTRIBUTING.md). The
GoogleTest section below is the quick reference; `STYLE_CPP.md` is canonical.

Build & test: `bazel test //...` · sanitizers: `bazel test //... --config=clang --config=asan`.
Toolchain: clang-22 minimum (hermetic LLVM under `--config=clang`).

## Writing tests (GoogleTest)

1. **Always `TEST_F` with a fixture; never bare `TEST(...)`** - even when the
   harness is a one-line `struct FooTest : ::testing::Test {};`.
2. **The fixture is a `struct`** (not a `class`) inheriting from
   `::testing::Test` (or a friend such as `::testing::TestWithParam<T>`).
3. **Prefer `EXPECT_THAT` / `ASSERT_THAT` (gmock matchers) almost exclusively**
   over `EXPECT_EQ` / `ASSERT_EQ`.
4. **Exception:** use `EXPECT_EQ` for **multiline text** comparisons - its
   unified-diff output is more readable than matchers for large strings.
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
- the hand-maintained `kHelpText` usage page in `cli/main.cc`;
- any prose docs the change affects (`docs/design-*.md`, `TODO.md`).

The generated `--help` / `--man` / `--markdown` then stay complete by construction.

## CLI conventions

- **Flag scope by dash count.** `--flag` is a whole-run global (a config / output /
  traversal modifier); a single-dash `-flag` is an expression primary (a per-entry
  test or action). Grep / GNU single-dash _globals_ (`-h`, `-help`, `-version`,
  `-q`) are deliberate special-cased compatibility aliases of their `--` form, not
  new primaries.
- **Flag-only; no subcommands.** xff is a single-purpose tool (like `fd` /
  `ripgrep`), so meta operations are flags (`--help`, `--man`, `--markdown`,
  `--explain`), never `git`-style subcommands; find and xff share one grammar,
  differing only in vocabulary. (Decided 2026-06-28.)
- **A user-toggleable boolean capability is a `--feature`, not a bespoke flag.**
  `--feature=NAME` / `--feature=no-NAME` is the parked mechanism (full design in
  [`TODO.md`](TODO.md) under #73); it has no concrete customer yet, so it is
  unbuilt. **Trigger:** the first time you would add a boolean on/off capability
  that is neither a whole-style behavior nor a valued option (valued ones stay
  dedicated flags, e.g. `--implicit-print=no`), build the `--feature` mechanism per
  the #73 design and register the capability there - do not add a one-off boolean
  flag.
