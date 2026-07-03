# Golden fixtures and expectations

Data for the `xff_golden` dual-mode tests (see [`../golden.bzl`](../golden.bzl) and the
cases in [`../BUILD.bazel`](../BUILD.bazel)). Each case runs the built `xff` over the
fixture tree once per style and compares stdout+stderr to a committed golden.

## Files

- `basic_tree.sh` - the shared fixture. Run with `$PWD` at a fresh temp root, it populates
  a small, deterministic tree (byte sizes are load-bearing: `--summary` asserts the total).
- `<case>.find.txt` / `<case>.xff.txt` - the expected **normalized** output under
  `--config=find` and `--config=xff` respectively. The two differ wherever the styles do
  (for example `summary.*`: find reports raw bytes, xff human sizes).

## The normalization contract

The driver captures `stdout` + `stderr`, then, so a golden is stable across machines and
runs:

1. replaces the temp root path with the literal `<ROOT>`, and
2. sorts the lines (unless the case sets `ordered = True`), since find's readdir order is
   not deterministic.

Exit status is not asserted; assert observable behavior through the output (an error line,
a summary total, the set of printed paths).

A case's `args` place the search root themselves via the `<ROOT>` token, so a global that
must precede the root (such as `--summary`) can: `["--summary", "<ROOT>", "-type", "f"]`.

## Adding a case

1. Add an `xff_golden(...)` target in `../BUILD.bazel` (reuse `basic_tree.sh` or add a new
   fixture script here).
2. Create empty golden files, run the test once, and paste the actual output from the
   failure diff into them - or generate them the same way the driver does: run
   `xff --config=<style> <args-with-$TREE>` and pipe through `sed 's|$TREE|<ROOT>|g' |
LC_ALL=C sort`.

## Updating goldens after an intended behavior change

Run the affected test; `expect_eq` prints expected-vs-actual on mismatch. Confirm the new
output is correct and paste it into the golden. Keep find and xff goldens in sync with the
change (an intended divergence belongs in exactly one of them).
