# TODO

Actionable roadmap and deliberately deferred ideas. Completed implementation records live in
[`docs/history-roadmap.md`](docs/history-roadmap.md); other resolved design records and investigations
live in [`docs/history.md`](docs/history.md).

## Project constraints

### Minimum Bazel version

The supported minimum is **Bazel 9.1.1**. Development and CI use the newer release pinned in
`.bazelversion`; the minimum is a support floor, not a second CI configuration. Bazel 7 is no longer
supported, and no Bazel 8 patch release is part of the support contract. Lowering the minimum requires
an explicit decision.

### Coverage policy

Every current coverage group meets the shared high boundaries. Category overrides may make enforcement
stricter; there are no lowered onboarding overrides. The JSON policy remains the single source of truth
for enforcement and presentation.

## Design required before implementation

1. **Reassembled shard contents (shards v2).** Decide whether the logical whole replaces or accompanies
   physical shards for matching and actions, how incomplete and duplicate sets read, and which path and
   metadata the synthetic entry owns. The shipped v1 already covers display, statistics, completeness,
   duplicates, and `-shard-status` matching; do not infer v2 semantics from it.
2. **Near-duplicate grouping.** Design a run-wide reduction using scalable candidate generation (likely
   MinHash, optionally a Bloom pre-filter) followed by exact Jaccard verification. If its data or
   dependency cost is substantial, make it an extension. False positives may trigger verification but
   must never enter emitted clusters.
3. **PHAR structural exposure.** A format-defined file such as `.phar/stub.php` remains file-like, with a
   stored member winning name conflicts. Decide whether a presentation option may hide such parts.
   Aliases, serialized metadata, and signatures are not files and remain unexported unless a coherent
   separate metadata model is designed.

## Deferred refinements

- Filesystem-name behavior: Unicode normalization and Linux per-directory case-fold detection.
- Per-summary-sink modifiers.
- Shell-glob numeric/alphabetic brace ranges.
- Histogram time buckets, explicit bucket edges/counts, and per-line template measures.
- Text-flavor mixed-line-ending policies.
- Post-1.0 behavior migration through `--unstable=NAME` if a concrete unstable spelling needs it;
  do not build the rejected general `--feature` registry speculatively.
- C++ header modules after the hermetic toolchain supplies real compile/use actions.
