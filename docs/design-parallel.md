# xff - Parallel Traversal Design

> Detailed spec for parallel directory traversal and `--sort` (issue #43).
> **Refines** `design.md` §"Cross-cutting concerns" (Determinism). Where the two
> differ, this document is authoritative for traversal/ordering specifically.
> Status: **Draft** · 2026-06-27 · Org: helly25

## Purpose

The directory walk is the hot path and it is IO-bound: most time is spent
blocked in `readdir`/`lstat`, not in CPU. Running it across several workers
hides that latency and is the single biggest performance lever for xff,
especially on deep trees and high-latency (network) filesystems.

Parallelism must not cost correctness. Three things are trivially correct in the
current sequential walk and must stay correct under concurrency: deterministic
output (when asked for), the `-prune`/`-quit`/`-depth` control semantics, and the
exit-code model. The VFS layer is already contractually safe to call from many
threads (`vfs::FileSystem`), so the foundation is ready.

## v1 scope (what shipped first)

The first implementation (engine PR for #43) takes three deliberate simplifications
of the design below; each has a noted follow-up:

- **The worker pool parallelizes `readdir`+`lstat` only.** The visitor (evaluate +
  emit + exec + capture + summary) runs on the single coordinator thread in `--sort`
  order, so that whole pipeline stays single-threaded and unchanged (`run.cc`
  untouched) - the only new concurrent code is the pool, whose jobs are pure
  (path -> stat'd listing) and touch just the thread-safe VFS and a mutex-guarded
  queue. Per-worker parallel evaluation is a later option if profiling wants it.
- **The ordered modes are deterministic.** Siblings are consumed in sorted order
  (reads still overlap via prefetch), so `dir`/`subtree`/`tree` are reproducible
  across runs and machines. Streaming subtrees strictly by completion order (lower
  latency, nondeterministic) is a later refinement.
- **Parallelism and sort are opt-in:** `-j`/`--jobs` and `--sort` (default stays
  sequential + `none`, find-compatible). The mode-scoped auto-defaults (modern ->
  parallel + `dir`; find/fd/rg -> all cores + `none`) land with the mode mechanism
  (#54), which is where a persona can be queried.

## Architecture

A bounded **worker pool** over directories, with a single **emission-ordering
layer** between the workers and the output sink:

- A work queue holds directories to read. Each worker pops a directory, lists it
  (`ReadDir` + `lstat`), evaluates each entry against the expression, hands any
  matches to the ordering layer, and pushes child directories back onto the
  queue. The walk ends when the queue drains and all workers are idle.
- `Evaluate` runs concurrently across workers. It reads per-entry state only;
  any side effects with shared state (capture aggregation, `--summary`, the emit
  sink) are funnelled through the ordering layer or otherwise serialized.
- The **ordering layer owns the sink** - it is the only writer. This keeps
  matching parallel while emission stays single-writer and correctly ordered for
  the chosen `--sort` mode. `-exec`/`-capture` children are bounded by the same
  `-j` budget (see below).

## `--sort` modes

Four modes, a spectrum of rising ordering cost. `name` stays an alias for `dir`
(back-compat with the current `SortOrder::kName`).

| `--sort`  | Buffer      | Deterministic order |
| --------- | ----------- | ------------------- |
| `none`    | none        | no                  |
| `dir`     | ~none       | within a directory  |
| `subtree` | bounded     | within a subtree    |
| `tree`    | all matches | fully               |

- **`none`** - emit entries in discovery (readdir) order as workers produce them.
  Pure streaming, no buffering, fully nondeterministic. Fastest.
- **`dir`** - each directory emits its complete direct listing (files **and**
  subdirectory entries) sorted as one block; the listing-blocks of different
  directories interleave by completion order. Buffers only one listing at a time.
  A directory's direct children are ordered; the tree as a whole is not.
- **`subtree`** - each directory emits its non-directory entries sorted, then
  inlines each subdirectory's **whole subtree contiguously** as that subtree
  completes. Subtrees come out as contiguous, locally-sorted blocks; sibling
  subtree order is nondeterministic. Buffers an in-flight subtree's output to
  keep it contiguous - bounded by worker count, not tree size.
- **`tree`** - the entire result set is globally path-ordered (subdirectories
  sorted into position too), fully reproducible across runs and machines.
  Buffers all matches (collect-all, or a hierarchical ordered merge). Most
  memory, slowest. `subtree` is the bounded-memory alternative when only
  contiguity (not a global order) is needed.

The key distinction between `dir`/`subtree` and `tree`: neither `dir` nor
`subtree` ever inlines a subdirectory's contents at a globally-sorted position -
that is precisely what `tree` buys, and why only `tree` must buffer everything.

## Parallelism control

A single knob, `-j N` (long form `--jobs`), caps total concurrency for **both**
the directory walk and concurrent `-exec`/`-capture` children - one mental model.
`-j 1` forces the sequential walk. `-j all` (`--jobs=all`) means every detected
core (`hardware_concurrency()`), regardless of the active mode's default.

Under `-j > 1` the serial `-exec ... ;` / `-execdir ... ;` form launches its child
on a bounded runner (at most `N` outstanding) instead of running it synchronously.
Because the child's exit status is not yet known when the action returns, the action
reports success on launch, so its truth value cannot gate a predicate to its right
(e.g. `-exec a \; -exec b \;` or `-exec test \; -print`). This matches find's exit
model: find's `;` form is a predicate whose nonzero exit makes only the action false
and never raises find's own exit status (verified against BSD and GNU find), so
nothing observable is lost at the exit-code level - the children are still reaped at
the end-of-walk drain. Use `-j 1` for find's strict synchronous, status-gating
semantics. The `+` batch forms are unaffected: they always accumulate and flush once
at end-of-walk, and a nonzero exit there does raise the exit status, as in find.

When `-j` is omitted the default is **mode-scoped** - xff wears the persona it is
invoked as (the `--mode` / `argv[0]` mechanism, design-config.md and #54/#59):

| Persona  | Workers (no `-j`)          | Default `--sort` |
| -------- | -------------------------- | ---------------- |
| `find`   | all cores                  | `none`           |
| `fd`     | all cores                  | `none`           |
| `rg`     | all cores                  | `none`           |
| `modern` | `max(1, min(cores-1, 15))` | `dir`            |

- The compatibility personas match their namesakes: `fd`/`rg` saturate cores,
  and `find`'s output is unordered. All cores = `hardware_concurrency()`.
- `modern` is the good-citizen persona: leave a core for the pipe consumer, cap
  at 15 to avoid oversubscription on many-core hosts, floor at 1 for single-core,
  and default to `dir` so output is nicely sorted out of the box.
- `-j N` overrides the worker count in every mode; `--sort=...` overrides the
  default ordering in every mode.

## Concurrency correctness

- **`-prune`** - a worker decides prune before enqueuing a directory's children,
  so a pruned subtree is simply never queued. Unchanged semantics.
- **`-quit`** - sets a stop flag the workers observe between entries; in-flight
  work drains and the queue is abandoned. Exit status follows the normal model.
- **`-depth` (post-order)** - children before parent. The ordering layer holds a
  directory's own visit until its subtree has been emitted; under `none` this
  still means a parent waits on its descendants' completion.
- **`-maxdepth`/`-mindepth`/`-xdev`** - per-entry decisions, unaffected by
  concurrency.
- **Exit-code model** - per-path errors are reported through a thread-safe error
  sink and folded into the final status (design.md "Exit-code model"); a partial
  failure still yields the right nonzero code.
- **Thread-safety** - `vfs::FileSystem` is already documented thread-safe. The
  emit sink, capture map, and `--summary` accumulators are written only by the
  ordering layer (single-writer) or sharded per worker and merged at the end.

## ThreadSanitizer

As soon as the walk is multithreaded we add a TSan run. TSan is mutually
exclusive with ASan, so it is a **separate** `--config=tsan` in `.bazelrc`
(`-fsanitize=thread` copt/linkopt, `TSAN_OPTIONS=halt_on_error=1`, the symbolizer,
mirroring the `asan` block) **and** its own `clang-tsan` CI matrix cell wired into
the `done` gate. It lands in the same PR that introduces threads (it is a no-op on
single-threaded code). The `asan` config also runs UBSan as of #138.

## Phasing (chained PRs)

Built as a chain of stacked PRs (each off its predecessor's tip), so each step is
small and conflict-free:

1. **Worker pool + `--sort=none`** - parallel walk behind the existing sink, plus
   the `clang-tsan` config and CI cell (threads arrive here, so TSan does too).
   The sequential walk stays available via `-j 1`.
2. **Ordering layer + `--sort=dir`** - generalize `SortOrder::kName` to `kDir`
   over the parallel walk (per-directory sorted listing blocks).
3. **`--sort=subtree`** - the bounded-buffer contiguous-subtree mode (`kSubtree`).
4. **`--sort=tree`** - the collect-all global ordering (`kTree`).
5. **`-j` / `--jobs` + mode-scoped defaults** - the flag and the per-persona
   worker-count and default-`--sort` wiring.

Open follow-ups: an optional stderr soft-warn for `--sort=tree` past a large
match count (no hard cap; `subtree` already covers bounded-memory ordering).
