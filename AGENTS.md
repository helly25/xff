# Agent rules — xff

Conventions for AI agents and contributors working on this repository.
Companion to [`docs/design.md`](docs/design.md) (decisions) and
[`docs/implementation-plan.md`](docs/implementation-plan.md) (build & sequencing).

Build & test: `bazel test //...` · sanitizers: `bazel test //... --config=clang --config=asan`.
Toolchain: clang-22 minimum (hermetic LLVM under `--config=clang`).

## Writing tests (GoogleTest)

1. **Always `TEST_F` with a fixture; never bare `TEST(...)`** — even when the
   harness is a one-line `struct FooTest : ::testing::Test {};`.
2. **The fixture is a `struct`** (not a `class`) inheriting from
   `::testing::Test` (or a friend such as `::testing::TestWithParam<T>`).
3. **Prefer `EXPECT_THAT` / `ASSERT_THAT` (gmock matchers) almost exclusively**
   over `EXPECT_EQ` / `ASSERT_EQ`.
4. **Exception:** use `EXPECT_EQ` for **multiline text** comparisons — its
   unified-diff output is more readable than matchers for large strings.
5. **Typed and parameterized tests supply names from the types/values** (name
   generators for `TYPED_TEST_SUITE` / `INSTANTIATE_TEST_SUITE_P`), so the
   output never shows numbered tests (`Suite/0`, `Suite/1`).
