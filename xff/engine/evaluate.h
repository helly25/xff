// SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com)
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef XFF_ENGINE_EVALUATE_H_
#define XFF_ENGINE_EVALUATE_H_

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "xff/engine/walk.h"
#include "xff/format/format.h"
#include "xff/parser/ast.h"
#include "xff/vfs/filesystem.h"

namespace xff::exec {
class ParallelExec;  // bounded concurrent `-exec/-execdir ... ;` runner (xff/exec/exec.h)
}  // namespace xff::exec

namespace xff::engine {

// Output sink for actions: receives one fully-formed record per action firing
// (path plus terminator -- "p\n" for -print, "p\0" for -print0).
using EmitFn = absl::FunctionRef<void(std::string_view)>;

// Traversal-control side-channel, set by control actions during evaluation and
// read by the driver: -prune asks the walk not to descend into the current
// directory; -quit asks it to stop the traversal entirely. Like `emit`, these
// fire as a side effect of reaching the action node, so short-circuit governs
// whether they are set.
struct Control {
  bool prune = false;
  bool quit = false;
  // Set by a predicate that cannot be evaluated correctly on this entry's
  // filesystem/kernel -- an "impossible task", e.g. -Btime where the birth time is
  // unrecorded. Holds a short static reason (empty means none). The driver turns it
  // into a hard error naming the path (exit 2) or, under --skip-unsupported, a
  // warning, skipping the entry. (Rule: fail when correctness is impossible; warn
  // when degraded-but-correct.) The predicate also returns false (it cannot match).
  std::string_view unsupported;
};

// Per-evaluation environment threaded through Evaluate for one visited entry.
// Bundles what an expression node may read -- the entry, the action sink, the
// filesystem, the reference clock -- plus the traversal-control side-channel, so
// predicates and actions take them from one place rather than a long parameter
// list. Evaluation-wide options (e.g. modern -exec field substitution) attach
// here as the engine grows.
struct EvalContext {
  const Visit& visit;  // the entry being evaluated (constant across the expression)
  EmitFn emit;         // action output sink (-print/-print0/...)
  // File-output sink for -fprint/-fprint0/-fprintf/-fls: (filename, record). The
  // driver appends to each named file (opened once, truncating); empty -> the
  // file actions are inert (in-process callers that wire no sink).
  std::function<void(std::string_view, std::string_view)> emit_file;
  // Row sink for -ls's aligned output: receives the entry's -ls columns as cells
  // (inode, blocks, perms, ...) for the driver to feed to a format::ColumnBuffer.
  // Empty -> -ls falls back to a single-space-joined line (in-process callers).
  std::function<void(std::vector<std::string>)> emit_ls_row;
  // -ls size-column rendering: human units (iec/si) or, when nullopt, raw bytes.
  // The driver resolves it from --human + the style (xff -> human, find -> bytes).
  std::optional<format::SizeUnits> ls_size_units;
  const vfs::FileSystem& fs;                  // backs predicates that read the source (e.g. -empty on a directory)
  absl::Time now;                             // single reference instant for age tests (-mtime/-mmin)
  absl::TimeZone tz = absl::LocalTimeZone();  // zone for interpreting time-string args (-newerXt); --timezone
  std::string_view time_format;               // --time-format: default for a time field with no {:qualifier}
  std::uint64_t block_size = 512;             // --block-size: bytes per -size block (bare value / 'b'); find's 512
  // FS-native name matching: when true, the case-sensitive name predicates
  // (-name/-path) fold case for this entry because it lives on a case-folding
  // volume and the xff-style default is in effect (no --exact). The driver
  // resolves it per entry from a per-device IsCaseSensitive probe; it is always
  // false in the find style and under --exact (byte-exact matching). The `i`
  // variants (-iname/-ipath) fold regardless.
  bool fold_name_case = false;
  // --regextype=EXACT: -grep matches its pattern as a literal substring per line
  // instead of the default RE2 regex. The driver resolves it once from --regextype
  // (RE2 the default; MATCH/PCRE reserved for #85). Only -grep consults it today.
  bool grep_literal = false;
  // --count / -c: -grep prints one `path:count` per file (its matching-line count)
  // instead of the lines, rg -c style; supersedes -grep=FORMAT. Only -grep reads it.
  bool grep_count = false;
  // --diff-algorithm=naive|direct|myers: the diff engine -diff uses (mbo::diff). Empty ->
  // myers (the default). Validated once before the walk; only -diff reads it.
  std::string_view diff_algorithm;
  // --diff-ignore=<tokens>: comma-separated normalization tokens for -diff (ws / change /
  // trail / blank / case), validated once before the walk. Empty -> exact. Only -diff reads it.
  std::string_view diff_ignore;
  // --diff-ignore-matching=REGEX: -diff ignores lines matching this RE2 (compiled per -diff
  // entry; the pattern is validated once before the walk). Empty -> no line filter.
  std::string_view diff_ignore_matching;
  Control& control;                              // collects -prune/-quit requests
  bool exec_fields = false;                      // --exec-fields: render -exec tokens through the field vocabulary
  std::vector<std::string>* captures = nullptr;  // -regex groups for gated -exec {0}..{N}; null when off
  const std::map<std::string, std::string>* defines = nullptr;  // --define values for {def.NAME}
  std::map<std::string, std::string>* outputs = nullptr;  // -capture results for {capture.NAME} (mutable, per entry)
  std::function<bool(std::string_view)> confirm;  // -ok prompt sink: returns true to run the command; empty -> decline
  // Accumulates matched items per `-exec/-execdir ... +` node for the end-of-walk
  // batch flush: outer key the Expr node, inner key the directory ("" for -exec's
  // single global batch; the entry's dir for -execdir's per-directory batches).
  // Null disables batching (the action no-ops).
  std::map<const parser::Expr*, std::map<std::string, std::vector<std::string>>>* exec_batches = nullptr;
  // Bounded concurrent runner for `-exec/-execdir ... ;` under -j>1: when set, the
  // serial-`;` action launches the child here (returning true on launch) instead of
  // running it synchronously. Null -> the action runs synchronously (find's default,
  // and -j1). The `+` batch forms always go through exec_batches, never this.
  exec::ParallelExec* parallel_exec = nullptr;
};

// Evaluates a parsed find expression against one visited entry and returns its
// overall truth value, mirroring find:
//   - tests: -name/-iname/-path/-ipath/-type/-true/-false against the entry;
//   - operators: !/-not, -a/-and, -o/-or, with short-circuit;
//   - actions: -print/-print0 write a record via `emit`; -prune/-quit set `control`.
// -name/-iname glob the basename, -path/-ipath glob the whole path; the `i`
// variants fold case (fnmatch, matching GNU find). Short-circuit means actions
// to the right of a failed -a (or in the unused branch of -o) do not fire.
// `context` carries the entry, the action sink, the filesystem, the reference
// clock, and the -prune/-quit side-channel (see EvalContext).
bool Evaluate(const parser::Expr& expr, EvalContext& context);

// True if `expr` contains any action node (-print, ...). The driver uses this
// to decide whether an implicit -print applies: find adds -print only when the
// expression has no action of its own.
bool ContainsAction(const parser::Expr& expr);

// Validates every `-size` argument in `expr`, returning the first malformed one as
// an InvalidArgument status (unknown unit, an over-64-bit unit like Z/Y, or a
// missing/non-numeric count) or Ok when all are well-formed. The driver calls this
// before the walk so a bad `-size` is a usage error (exit 2) rather than a silent
// per-entry no-match, matching find's parse-time rejection. Style-independent: the
// size units (incl. the T/P/E continuation) are valid in every flavor.
absl::Status ValidateSizeArgs(const parser::Expr& expr);

// Validates the --diff-ignore token list and the --diff-ignore-matching regex, returning the
// first problem as an InvalidArgument (an unknown token names it; a bad regex carries RE2's
// diagnostic) or Ok. Each comma-separated token in `tokens` must be one of ws / change / trail
// / blank / case (empty tokens are skipped); a non-empty `matching` must be a valid RE2. The
// driver calls it once before the walk so a bad value is a usage error (exit 2) rather than a
// silent per-entry no-op; -diff then trusts the validated values.
absl::Status ValidateDiffIgnore(std::string_view tokens, std::string_view matching);

// Parses a `--block-size=SIZE` value into bytes: `N[unit]` where a bare number is
// bytes and the unit suffixes are the fixed binary multiples (c/w/k/M/G/T/P/E; 'b'
// and the over-64-bit Z/Y/... are rejected). The result is the bytes-per-block used
// for a bare `-size N` and the `-size Nb` unit (find's default is 512). Returns an
// InvalidArgument status (naming the problem) for a malformed or zero size.
absl::StatusOr<std::uint64_t> ParseBlockSize(std::string_view spec);

// The `-ls` columns for one entry, in display order: inode, 1 KiB blocks, symbolic
// permissions, link count, owner, group, size, time (in `tz`), path. The size is
// rendered in `size_units` (human iec/si) or, when nullopt, as raw bytes. The
// aligned renderer (format::ColumnBuffer) consumes these; LsRecord joins them
// single-spaced for the unbuffered fallback.
std::vector<std::string> LsCells(
    const Visit& visit,
    absl::Time now,
    absl::TimeZone tz,
    std::optional<format::SizeUnits> size_units);

// One `-ls` column's alignment and minimum (also fixed, when --buffer=off) width.
struct LsColumn {
  format::Align align;
  std::size_t min_width;
};

// The `-ls` column layout (alignment + minimum width per column), in LsCells order:
// numeric columns right-aligned, text left; the driver builds the --buffer
// format::ColumnBuffer from this. The minimums are ls -l-like defaults for the
// flexible fields (owner/group/size), a fixed 10 for the permission string, and 0
// for the trailing path.
std::vector<LsColumn> LsColumns();

// The -printf directive vocabulary for `--help=printf`, as {code, description} rows:
// find's % directives (the kPrintfDirectives table), the a/c/t + Ak/Ck/Tk time families,
// the \ escapes, and xff's %{field} escape. evaluate_test guards that it documents every
// directive in PrintfDirectiveLetters().
std::vector<std::pair<std::string_view, std::string_view>> PrintfDocs();

// The letters of find's -printf % directives (the kPrintfDirectives keys), for the
// `--help=printf` coverage guard.
std::string PrintfDirectiveLetters();

// The -size unit vocabulary for `--help=size`, as {code, description} rows: the unit
// suffixes (kSizeUnits), the 512-byte block default, and the +/- comparison prefix.
// evaluate_test guards that it covers SizeUnitSuffixes().
std::vector<std::pair<std::string_view, std::string_view>> SizeUnitDocs();

// The -size unit suffixes (the kSizeUnits keys), for the `--help=size` coverage guard.
std::string SizeUnitSuffixes();

}  // namespace xff::engine

#endif  // XFF_ENGINE_EVALUATE_H_
