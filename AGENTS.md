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
