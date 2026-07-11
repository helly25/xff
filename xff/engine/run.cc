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

#include "xff/engine/run.h"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/time/time.h"
#include "mbo/container/limited_map.h"
#include "mbo/diff/diff_options.h"
#include "xff/color/color.h"
#include "xff/content/line_match.h"
#include "xff/datetime/datetime.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/walk.h"
#include "xff/exec/exec.h"
#include "xff/fields/fields.h"
#include "xff/format/format.h"
#include "xff/hash/hash.h"
#include "xff/ignore/ignore.h"
#include "xff/language/language.h"
#include "xff/parser/ast.h"
#include "xff/parser/parser.h"
#include "xff/regex/regex.h"
#include "xff/registry/descriptor.h"
#include "xff/render/render.h"
#include "xff/repo/repo.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

// Parses a non-negative decimal integer (find depth arguments).
bool ParseNonNegInt(std::string_view text, int* out) {
  if (text.empty()) {
    return false;
  }
  int value = 0;
  for (const char digit : text) {
    if (digit < '0' || digit > '9') {
      return false;
    }
    value = value * 10 + (digit - '0');
  }
  *out = value;
  return true;
}

// find treats -maxdepth/-mindepth/-depth/-xdev as global positional options
// (they apply regardless of where they sit in the expression); collect them into
// the walk limits. Last occurrence wins, as in find.
void ScanDepthOptions(const parser::Expr& expr, WalkOptions* options) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: {
      if (expr.descriptor->name == "-depth" || expr.descriptor->name == "-d" || expr.descriptor->name == "-delete") {
        options->post_order = true;  // -delete implies -depth; -d is the BSD/GNU short spelling
        break;
      }
      if (expr.descriptor->name == "-xdev" || expr.descriptor->name == "-mount" || expr.descriptor->name == "-x") {
        options->single_filesystem = true;  // -mount (GNU/BSD) and -x (BSD) are synonyms for -xdev
        break;
      }
      if (expr.descriptor->name == "-ignore_readdir_race") {
        options->ignore_readdir_race = true;
        break;
      }
      if (expr.descriptor->name == "-noignore_readdir_race") {
        options->ignore_readdir_race = false;  // last occurrence wins, as in find
        break;
      }
      int value = 0;
      if (!expr.args.empty() && ParseNonNegInt(expr.args.front(), &value)) {
        if (expr.descriptor->name == "-maxdepth") {
          options->max_depth = value;
        } else if (expr.descriptor->name == "-mindepth") {
          options->min_depth = value;
        }
      }
      break;
    }
    case parser::Expr::Kind::kNot: ScanDepthOptions(*expr.lhs, options); break;
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXor:
    case parser::Expr::Kind::kXnor:
    case parser::Expr::Kind::kComma:
      ScanDepthOptions(*expr.lhs, options);
      ScanDepthOptions(*expr.rhs, options);
      break;
  }
}

// find's -H/-L/-P select symlink handling; they are leading global options and
// the last one wins (default -P). The parser collects them in command.globals.
SymlinkMode ResolveSymlinkMode(const std::vector<std::string>& globals) {
  SymlinkMode mode = SymlinkMode::kNever;
  for (const std::string& global : globals) {
    if (global == "-P") {
      mode = SymlinkMode::kNever;
    } else if (global == "-L") {
      mode = SymlinkMode::kAll;
    } else if (global == "-H") {
      mode = SymlinkMode::kRoots;
    }
  }
  return mode;
}

// The mode-scoped default worker count when `-j` is absent (docs/design-parallel.md
// "Parallelism control"): modern (kXff) leaves a core for the consumer and caps at
// 15 to avoid oversubscription; find/fd/rg saturate cores; an unset style stays
// sequential (the conservative in-process / test default).
std::size_t DefaultWorkers(std::optional<registry::Style> style) {
  if (!style.has_value()) {
    return 1;
  }
  const unsigned detected = std::thread::hardware_concurrency();
  const std::size_t cores = detected == 0 ? 1 : detected;
  if (*style == registry::Style::kXff) {
    return std::max<std::size_t>(1, std::min<std::size_t>(cores - 1, 15));
  }
  return cores;
}

// xff --sort=none|dir|subtree|tree: per-directory sibling ordering for the walk
// (see docs/design-parallel.md). `none` keeps readdir order (find's default);
// `dir` sorts each directory's listing; `subtree` adds contiguous subtrees;
// `tree` is a total path order. Bare --sort and the legacy `name` mean `dir`. The
// default is mode-scoped: modern (kXff) sorts each directory, find stays unordered.
// Leading global, last occurrence wins.
SortOrder ResolveSort(const std::vector<std::string>& globals, std::optional<registry::Style> style) {
  SortOrder sort = style == registry::Style::kXff ? SortOrder::kDir : SortOrder::kNone;
  for (const std::string& global : globals) {
    if (global == "--sort" || global == "--sort=dir" || global == "--sort=name") {
      sort = SortOrder::kDir;
    } else if (global == "--sort=subtree") {
      sort = SortOrder::kSubtree;
    } else if (global == "--sort=tree") {
      sort = SortOrder::kTree;
    } else if (global == "--sort=none") {
      sort = SortOrder::kNone;
    }
  }
  return sort;
}

// xff -jN / --jobs=N: worker threads for the parallel directory read-ahead (see
// docs/design-parallel.md). When absent, the count is mode-scoped (DefaultWorkers).
// Leading global, last valid occurrence wins; a non-positive or unparseable value
// is ignored.
std::size_t ResolveJobs(const std::vector<std::string>& globals, std::optional<registry::Style> style) {
  std::size_t jobs = DefaultWorkers(style);
  for (const std::string& global : globals) {
    std::string_view value;
    if (global.starts_with("--jobs=")) {
      value = std::string_view(global).substr(7);
    } else if (global.starts_with("-j") && global.size() > 2) {
      value = std::string_view(global).substr(2);
    } else {
      continue;
    }
    if (value == "all") {  // --jobs=all / -jall: every detected core, regardless of mode
      const unsigned detected = std::thread::hardware_concurrency();
      jobs = detected == 0 ? 1 : detected;
      continue;
    }
    std::size_t parsed = 0;
    if (absl::SimpleAtoi(value, &parsed) && parsed >= 1) {
      jobs = parsed;
    }
  }
  return jobs;
}

// xff --summary[=overall|type|ext|lang|mime|user|group]: reduce the matches to a count +
// total size table instead of printing each one. Bare --summary / =overall is one total row;
// =type groups by file type, =ext by filename extension, =lang by programming language, =mime by
// media type, =user (alias =owner) by owner name, =group by owning group (each a files-per-key
// histogram); =none / absent is off. The mime/user/group keys reuse the field vocabulary (the
// {mime}/{user}/{group} renderers), so they cannot drift from the field values.
enum class SummaryMode : std::uint8_t { kOff, kOverall, kType, kExt, kLanguage, kMime, kUser, kGroup, kTemplate };

SummaryMode ResolveSummary(const std::vector<std::string>& globals) {
  SummaryMode mode = SummaryMode::kOff;
  for (const std::string& global : globals) {
    if (global == "--summary" || global == "--summary=overall") {
      mode = SummaryMode::kOverall;
    } else if (global == "--summary=type") {
      mode = SummaryMode::kType;
    } else if (global == "--summary=ext") {
      mode = SummaryMode::kExt;
    } else if (global == "--summary=lang") {
      mode = SummaryMode::kLanguage;
    } else if (global == "--summary=mime") {
      mode = SummaryMode::kMime;
    } else if (global == "--summary=user" || global == "--summary=owner") {
      mode = SummaryMode::kUser;
    } else if (global == "--summary=group") {
      mode = SummaryMode::kGroup;
    } else if (global == "--summary=none") {
      mode = SummaryMode::kOff;
    } else if (global.starts_with("--summary={")) {
      mode = SummaryMode::kTemplate;  // group by a {field} template (incl. an m// per-line extraction)
    }
  }
  return mode;
}

// The `--summary={template}` key template (last occurrence), or empty when no template form was
// given. The `{...}` groups by an arbitrary field value; an m// extraction key groups per extracted
// line (a value stream), any other template one key per matched entry.
std::string SummaryKeyTemplate(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--summary=";
  std::string tmpl;
  for (const std::string& global : globals) {
    if (global.starts_with("--summary={")) {
      tmpl = global.substr(kPrefix.size());
    }
  }
  return tmpl;
}

// The readable file-type word used as a --summary=type group key, keyed by file
// type (kUnknown and any unmapped value fall through to "unknown"). A constexpr map,
// per the style's preference for a uniform key -> value mapping over a switch.
using TypeNamePair = std::pair<vfs::FileType, std::string_view>;
constexpr auto kTypeNames = mbo::container::MakeLimitedMap(
    TypeNamePair{vfs::FileType::kBlockDevice, "block-device"},
    TypeNamePair{vfs::FileType::kCharDevice, "char-device"},
    TypeNamePair{vfs::FileType::kDirectory, "directory"},
    TypeNamePair{vfs::FileType::kFifo, "fifo"},
    TypeNamePair{vfs::FileType::kRegular, "file"},
    TypeNamePair{vfs::FileType::kSocket, "socket"},
    TypeNamePair{vfs::FileType::kSymlink, "symlink"});

std::string_view TypeName(vfs::FileType type) {
  const auto it = kTypeNames.find(type);
  return it == kTypeNames.end() ? "unknown" : it->second;  // kUnknown / unmapped -> "unknown"
}

// The filename extension used as a --summary=ext group key: the part after the
// last '.', or "(none)" when there is none (including a leading-dot dotfile).
std::string SummaryExtension(std::string_view name) {
  const std::string_view::size_type dot = name.rfind('.');
  if (dot == std::string_view::npos || dot == 0) {
    return "(none)";
  }
  return std::string(name.substr(dot + 1));
}

// The group key for one matched entry under `mode` (kOff never reaches here). The mime/user/group
// keys render the matching field ({mime}/{user}/{group}) so the reduction reuses the field
// vocabulary rather than re-deriving the value; the field renderers never return empty (owner /
// group fall back to the numeric id, mime to application/octet-stream), so no "(none)" bucket.
std::string SummaryKey(SummaryMode mode, const Visit& visit) {
  switch (mode) {
    case SummaryMode::kExt: return SummaryExtension(visit.name);
    case SummaryMode::kType: return std::string(TypeName(visit.metadata.type));
    case SummaryMode::kLanguage: {
      const std::string_view lang = language::LanguageForName(visit.name);
      return lang.empty() ? "(none)" : std::string(lang);  // unrecognized -> one "(none)" bucket
    }
    case SummaryMode::kMime: return fields::Render("{mime}", visit.path, visit.metadata, visit.depth);
    case SummaryMode::kUser: return fields::Render("{user}", visit.path, visit.metadata, visit.depth);
    case SummaryMode::kGroup: return fields::Render("{group}", visit.path, visit.metadata, visit.depth);
    default: return "total";  // kOverall: a single bucket
  }
}

// xff --histogram=BUCKET[:MEASURE] (repeatable): reduce matches to a bar chart instead of (or
// alongside) the --summary table. BUCKET groups the matches - categorical (overall / type / ext /
// lang / mime / user / group) or a numeric-range field (size / lines by order of magnitude, depth
// per level). The optional MEASURE is the bar's value (see HistAgg). Independent of and combinable
// with --summary; both are terminal reductions fed by one walk.
enum class HistBucket : std::uint8_t {
  kOverall,
  kType,
  kExt,
  kLang,
  kMime,
  kUser,
  kGroup,
  kSizeRange,
  kLinesRange,
  kDepthRange,
};

// A numeric-range bucket (size / lines / depth) draws its bars in ascending range order; a
// categorical bucket sorts by bar height and honors --top.
constexpr bool IsNumericBucket(HistBucket bucket) {
  return bucket == HistBucket::kSizeRange || bucket == HistBucket::kLinesRange || bucket == HistBucket::kDepthRange;
}

// A histogram's measure: the count of matched entries (the default), or an aggregate of a numeric
// field. kCount ignores `metric`; the aggregators reduce the metric field per bucket.
enum class HistAgg : std::uint8_t { kCount, kSum, kMean, kMin, kMax };
enum class HistMetric : std::uint8_t { kSize, kLines };  // the numeric field an aggregate reduces

struct HistogramSpec {
  HistBucket bucket = HistBucket::kOverall;
  HistAgg agg = HistAgg::kCount;
  HistMetric metric = HistMetric::kSize;  // only when agg != kCount
  std::string label;                      // the spec as written, for the jsonl tag + text heading
};

// The running per-bucket aggregate. `label` is the bucket's display text (the map key itself for a
// categorical bucket; a range like "10-99" for a numeric one). min/max are valid iff count>0.
struct HistCell {
  std::string label;
  std::uint64_t count = 0;
  std::uint64_t sum = 0;
  std::uint64_t min = 0;
  std::uint64_t max = 0;
};

// Parses a `--histogram` MEASURE (the part after `BUCKET:`) into (aggregator, metric). "count" is
// the aggregator-free measure; every other form must be AGG(FIELD) with AGG in sum/mean/min/max and
// FIELD in size/lines. A bare field with no aggregator (e.g. `lines`) is a usage error, per design.
absl::StatusOr<std::pair<HistAgg, HistMetric>> ParseHistMeasure(std::string_view measure) {
  const std::string_view::size_type open = measure.find('(');
  if (open == std::string_view::npos || measure.empty() || measure.back() != ')') {
    return absl::InvalidArgumentError(
        absl::StrCat(
            "histogram metric '", measure, "' needs an aggregator: sum(size), mean(lines), min(...), or max(...)"));
  }
  const std::string_view agg_name = measure.substr(0, open);
  const std::string_view field = measure.substr(open + 1, measure.size() - open - 2);
  HistAgg agg = HistAgg::kSum;
  if (agg_name == "sum") {
    agg = HistAgg::kSum;
  } else if (agg_name == "mean") {
    agg = HistAgg::kMean;
  } else if (agg_name == "min") {
    agg = HistAgg::kMin;
  } else if (agg_name == "max") {
    agg = HistAgg::kMax;
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("unknown histogram aggregator '", agg_name, "'; use sum, mean, min, or max"));
  }
  HistMetric metric = HistMetric::kSize;
  if (field == "size") {
    metric = HistMetric::kSize;
  } else if (field == "lines") {
    metric = HistMetric::kLines;
  } else {
    return absl::InvalidArgumentError(absl::StrCat("unknown histogram metric field '", field, "'; use size or lines"));
  }
  return std::make_pair(agg, metric);
}

// An order-of-magnitude range bucket for `value`: {order-preserving map key, human range label}.
// "0" for zero, then "1-9", "10-99", "100-999", ...; the map key is the zero-padded magnitude so
// buckets sort ascending. Exact integer math (no log/pow rounding).
std::pair<std::string, std::string> MagnitudeBucket(std::uint64_t value) {
  if (value == 0) {
    return {"00", "0"};
  }
  int magnitude = 0;
  std::uint64_t low = 1;
  for (std::uint64_t rest = value; rest >= 10; rest /= 10) {
    ++magnitude;
    low *= 10;
  }
  return {absl::StrFormat("%02d", magnitude + 1), absl::StrCat(low, "-", (low * 10) - 1)};
}

// A per-value depth bucket: one bucket per level. The map key is zero-padded so 2 sorts before 10.
std::pair<std::string, std::string> DepthBucket(int depth) {
  return {absl::StrFormat("%06d", depth), absl::StrCat(depth)};
}

// The {order-preserving map key, display label} for `visit` under `spec`, or nullopt when the bucket
// field is unavailable (a numeric lines bucket for a non-regular or binary file). Categorical
// buckets reuse SummaryKey; numeric buckets range-bucket a field.
std::optional<std::pair<std::string, std::string>> HistBucketKey(const HistogramSpec& spec, const Visit& visit) {
  switch (spec.bucket) {
    case HistBucket::kOverall:
    case HistBucket::kType:
    case HistBucket::kExt:
    case HistBucket::kLang:
    case HistBucket::kMime:
    case HistBucket::kUser:
    case HistBucket::kGroup: {
      // Categorical buckets reuse SummaryKey; the map converts the bucket to its summary mode
      // (kOverall - and any unmapped value - is the single "total" key).
      using BucketModePair = std::pair<HistBucket, SummaryMode>;
      constexpr auto kBucketModes = mbo::container::MakeLimitedMap(
          BucketModePair{HistBucket::kType, SummaryMode::kType}, BucketModePair{HistBucket::kExt, SummaryMode::kExt},
          BucketModePair{HistBucket::kLang, SummaryMode::kLanguage},
          BucketModePair{HistBucket::kMime, SummaryMode::kMime}, BucketModePair{HistBucket::kUser, SummaryMode::kUser},
          BucketModePair{HistBucket::kGroup, SummaryMode::kGroup});
      const auto it = kBucketModes.find(spec.bucket);
      const SummaryMode mode = it == kBucketModes.end() ? SummaryMode::kOverall : it->second;
      std::string key = SummaryKey(mode, visit);
      return std::make_pair(key, key);
    }
    case HistBucket::kSizeRange: return MagnitudeBucket(visit.metadata.size);
    case HistBucket::kLinesRange: {
      const std::optional<std::uint64_t> lines =
          visit.metadata.type == vfs::FileType::kRegular ? content::FileLineCount(visit.path) : std::nullopt;
      return lines.has_value() ? std::optional(MagnitudeBucket(*lines)) : std::nullopt;
    }
    case HistBucket::kDepthRange: return DepthBucket(visit.depth);
  }
  return std::nullopt;
}

absl::StatusOr<std::vector<HistogramSpec>> ResolveHistograms(const std::vector<std::string>& globals) {
  constexpr std::string_view kFlag = "--histogram";
  std::vector<HistogramSpec> specs;
  for (std::string_view global : globals) {
    if (global != kFlag && !absl::StartsWith(global, absl::StrCat(kFlag, "="))) {
      continue;
    }
    const std::string_view value = global == kFlag ? "overall" : global.substr(kFlag.size() + 1);
    const std::string_view::size_type colon = value.find(':');
    const std::string_view bucket_name = value.substr(0, colon);
    HistogramSpec spec;
    spec.label = std::string(value);
    if (bucket_name == "overall") {
      spec.bucket = HistBucket::kOverall;
    } else if (bucket_name == "type") {
      spec.bucket = HistBucket::kType;
    } else if (bucket_name == "ext") {
      spec.bucket = HistBucket::kExt;
    } else if (bucket_name == "lang") {
      spec.bucket = HistBucket::kLang;
    } else if (bucket_name == "mime") {
      spec.bucket = HistBucket::kMime;
    } else if (bucket_name == "user" || bucket_name == "owner") {
      spec.bucket = HistBucket::kUser;
    } else if (bucket_name == "group") {
      spec.bucket = HistBucket::kGroup;
    } else if (bucket_name == "size") {
      spec.bucket = HistBucket::kSizeRange;
    } else if (bucket_name == "lines") {
      spec.bucket = HistBucket::kLinesRange;
    } else if (bucket_name == "depth") {
      spec.bucket = HistBucket::kDepthRange;
    } else {
      return absl::InvalidArgumentError(
          absl::StrCat(
              "unknown --histogram bucket '", bucket_name,
              "'; use overall, type, ext, lang, mime, user, group, size, lines, or depth"));
    }
    if (colon != std::string_view::npos && value.substr(colon + 1) != "count") {
      const absl::StatusOr<std::pair<HistAgg, HistMetric>> measure = ParseHistMeasure(value.substr(colon + 1));
      if (!measure.ok()) {
        return measure.status();
      }
      spec.agg = measure->first;
      spec.metric = measure->second;
    }
    specs.push_back(std::move(spec));
  }
  return specs;
}

// A horizontal bar `fraction` (0..1) of `width` cells wide. With unicode, the Unicode block
// elements give eighth-of-a-cell precision (full block plus a partial); otherwise ASCII '#'
// rounded to whole cells. Empty for fraction 0.
std::string HistogramBar(double fraction, std::size_t width, bool unicode) {
  fraction = std::clamp(fraction, 0.0, 1.0);
  if (!unicode) {
    return std::string(static_cast<std::size_t>(std::llround(fraction * static_cast<double>(width))), '#');
  }
  constexpr std::array<std::string_view, 8> kPartials = {"", "▏", "▎", "▍", "▌", "▋", "▊", "▉"};
  const auto eighths = static_cast<std::size_t>(std::llround(fraction * static_cast<double>(width) * 8.0));
  std::string bar;
  for (std::size_t full = eighths / 8; full > 0; --full) {
    bar += "█";  // full block
  }
  bar += kPartials.at(eighths % 8);
  return bar;
}

// The rendered aggregate for one bucket: a double for bar-scaling and sorting, an aligned text
// form, and the JSON number. mean is fixed to `precision` decimals; the integer aggregators
// (count / sum / min / max) group digits for the text form and emit a bare integer for JSON.
struct HistValue {
  double scale = 0;
  std::string text;
  std::string json;
};

HistValue HistMeasureValue(HistAgg agg, const HistCell& cell, unsigned precision) {
  const auto integer = [](std::uint64_t value) {
    return HistValue{.scale = static_cast<double>(value), .text = format::Int(value, ','), .json = absl::StrCat(value)};
  };
  switch (agg) {
    case HistAgg::kSum: return integer(cell.sum);
    case HistAgg::kMin: return integer(cell.min);
    case HistAgg::kMax: return integer(cell.max);
    case HistAgg::kMean: {
      const double mean = cell.count == 0 ? 0.0 : static_cast<double>(cell.sum) / static_cast<double>(cell.count);
      std::string formatted;
      if (!absl::FormatUntyped(
              &formatted, absl::UntypedFormatSpec(absl::StrCat("%.", precision, "f")), {absl::FormatArg(mean)})) {
        formatted = absl::StrCat(mean);  // defensive: the format is always valid
      }
      return HistValue{.scale = mean, .text = formatted, .json = formatted};
    }
    case HistAgg::kCount: break;
  }
  return integer(cell.count);
}

// Applies one --context SPEC onto (before, after): a bare non-negative integer sets both sides,
// else comma-separated A:N / B:N / C:N tokens set after / before / both (last value per side wins).
// A malformed token or count is an InvalidArgument.
absl::Status ApplyContextSpec(std::string_view spec, std::size_t& before, std::size_t& after) {
  if (std::size_t both = 0; absl::SimpleAtoi(spec, &both)) {
    before = both;
    after = both;
    return absl::OkStatus();
  }
  for (const std::string_view token : absl::StrSplit(spec, ',', absl::SkipEmpty())) {
    const std::size_t colon = token.find(':');
    std::size_t value = 0;
    if (colon == std::string_view::npos || !absl::SimpleAtoi(token.substr(colon + 1), &value)) {
      return absl::InvalidArgumentError(absl::StrCat("bad --context token '", token, "' (use N, or A:N / B:N / C:N)"));
    }
    const std::string_view side = token.substr(0, colon);
    if (side == "A" || side == "a") {
      after = value;
    } else if (side == "B" || side == "b") {
      before = value;
    } else if (side == "C" || side == "c") {
      before = value;
      after = value;
    } else {
      return absl::InvalidArgumentError(absl::StrCat("bad --context side '", side, "' (use A, B, or C)"));
    }
  }
  return absl::OkStatus();
}

// --context=SPEC / --before-context=N / --after-context=N (grep -C/-B/-A): the lines of context
// -grep prints before/after each match. Processed in order, last value per side wins; fills
// (before, after) and sets `any_context` when at least one of the three flags appeared (so a
// caller can tell a deliberate `--context=0` from "no context flag"). A malformed value is an
// InvalidArgument (a usage error before the walk).
absl::Status ResolveGrepContext(
    const std::vector<std::string>& globals,
    std::size_t& before,
    std::size_t& after,
    bool& any_context) {
  constexpr std::string_view kContext = "--context=";
  constexpr std::string_view kBefore = "--before-context=";
  constexpr std::string_view kAfter = "--after-context=";
  before = 0;
  after = 0;
  any_context = false;
  for (const std::string& global : globals) {
    if (global.starts_with(kContext)) {
      any_context = true;
      if (const absl::Status status = ApplyContextSpec(std::string_view(global).substr(kContext.size()), before, after);
          !status.ok()) {
        return status;
      }
    } else if (global.starts_with(kBefore)) {
      any_context = true;
      if (const std::string_view value = std::string_view(global).substr(kBefore.size());
          !absl::SimpleAtoi(value, &before)) {
        return absl::InvalidArgumentError(absl::StrCat("bad --before-context value '", value, "'"));
      }
    } else if (global.starts_with(kAfter)) {
      any_context = true;
      if (const std::string_view value = std::string_view(global).substr(kAfter.size());
          !absl::SimpleAtoi(value, &after)) {
        return absl::InvalidArgumentError(absl::StrCat("bad --after-context value '", value, "'"));
      }
    }
  }
  return absl::OkStatus();
}

// xff's modern output selector (leading globals, last wins, default plain):
// --format=plain|nul|jsonl, with -0 a shorthand for NUL. find's -print/-print0
// keep their fixed formats; this drives only the implicit (default) print.
render::Format ResolveFormat(const std::vector<std::string>& globals) {
  render::Format format = render::Format::kPlain;
  for (const std::string& global : globals) {
    if (global == "-0" || global == "--format=nul") {
      format = render::Format::kNul;
    } else if (global == "--format=plain") {
      format = render::Format::kPlain;
    } else if (global == "--format=jsonl") {
      format = render::Format::kJsonl;
    } else if (global == "--format=csv") {
      format = render::Format::kCsv;
    } else if (global == "--format=tsv") {
      format = render::Format::kTsv;
    } else if (global == "--format=aligned") {
      format = render::Format::kAligned;
    } else if (global == "--format=markdown" || global == "--format=md") {
      format = render::Format::kMarkdown;  // `md` is the short alias of the canonical `markdown`
    } else if (global == "--format=tree") {
      format = render::Format::kTree;
    }
  }
  return format;
}

// --unicode=auto|always|never: whether --format=tree draws Unicode box-drawing connectors (else
// ASCII). auto (the default) uses Unicode when the locale is UTF-8 (LC_ALL / LC_CTYPE / LANG,
// first set wins), the way --color=auto probes the tty. Last occurrence wins; bare --unicode ==
// --unicode=always.
bool ResolveUnicode(const std::vector<std::string>& globals) {
  enum class When : std::uint8_t { kAuto, kAlways, kNever };
  When when = When::kAuto;
  for (const std::string& global : globals) {
    if (global == "--unicode" || global == "--unicode=always") {
      when = When::kAlways;
    } else if (global == "--unicode=never") {
      when = When::kNever;
    } else if (global == "--unicode=auto") {
      when = When::kAuto;
    }
  }
  if (when != When::kAuto) {
    return when == When::kAlways;
  }
  for (const char* const var : {"LC_ALL", "LC_CTYPE", "LANG"}) {
    if (const char* const value = std::getenv(var); value != nullptr && *value != '\0') {
      return absl::StrContains(absl::AsciiStrToUpper(value), "UTF");  // en_US.UTF-8, C.UTF-8, ...
    }
  }
  return false;  // no locale set -> ASCII is the safe default
}

// --columns=FIELD,FIELD,... : the tabular column set for --format=csv / tsv, drawn from
// the {field} vocabulary (a column may carry a :qualifier, like {field}). Last occurrence
// wins; empty (absent) keeps the single default `path` column.
std::vector<std::string> ResolveColumns(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--columns=";
  std::vector<std::string> columns;
  for (const std::string& global : globals) {
    if (global.starts_with(kPrefix)) {
      columns = absl::StrSplit(std::string_view(global).substr(kPrefix.size()), ',');  // last --columns wins
    }
  }
  return columns;
}

// --path-encoding=raw|escape: how the plain renderer emits path bytes (see
// render::PathEncoding). Mirrors ResolveFormat -- last occurrence wins, the
// find-compatible kRaw default; applies only to the default/plain output.
render::PathEncoding ResolvePathEncoding(const std::vector<std::string>& globals) {
  render::PathEncoding encoding = render::PathEncoding::kRaw;
  for (const std::string& global : globals) {
    if (global == "--path-encoding=raw") {
      encoding = render::PathEncoding::kRaw;
    } else if (global == "--path-encoding=escape") {
      encoding = render::PathEncoding::kEscape;
    }
  }
  return encoding;
}

// --template=TMPL renders each match through the field vocabulary (xff-native),
// overriding --format for the implicit print. Last occurrence wins; nullopt when
// absent.
std::optional<std::string> ResolveTemplate(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--template=";
  std::optional<std::string> tmpl;
  for (const std::string& global : globals) {
    if (global.starts_with(kPrefix)) {
      tmpl = global.substr(kPrefix.size());
    }
  }
  return tmpl;
}

// --timezone=ZONE (short alias --tz=ZONE) overrides the detected local zone used to
// interpret time-string arguments (-newerXt). Last occurrence of either spelling
// wins. Resolves the winner to *tz and returns true; an unknown zone returns false
// with *bad set to the offending value (and *tz unchanged), so the caller can fail
// the run. Absent the flag, leaves *tz at its local-zone default and returns true.
bool ResolveTimeZone(const std::vector<std::string>& globals, absl::TimeZone* tz, std::string* bad) {
  std::optional<std::string> spec;
  for (const std::string& global : globals) {
    for (const std::string_view prefix : {std::string_view("--timezone="), std::string_view("--tz=")}) {
      if (global.starts_with(prefix)) {
        spec = global.substr(prefix.size());  // last occurrence of either spelling wins
      }
    }
  }
  if (!spec.has_value()) {
    return true;
  }
  if (!datetime::ParseTimeZone(*spec, tz)) {
    *bad = *std::move(spec);
    return false;
  }
  return true;
}

// --block-size=SIZE sets the bytes-per-block for a bare `-size N` and `-size Nb`
// (find's historical default is 512). Last occurrence wins. Resolves to *block_size
// (left untouched when the flag is absent) and returns Ok, or the parse error.
absl::Status ResolveBlockSize(const std::vector<std::string>& globals, std::uint64_t* block_size) {
  constexpr std::string_view kPrefix = "--block-size=";
  std::optional<std::string> spec;
  for (const std::string& global : globals) {
    if (global.starts_with(kPrefix)) {
      spec = global.substr(kPrefix.size());  // last occurrence wins
    }
  }
  if (!spec.has_value()) {
    return absl::OkStatus();
  }
  const absl::StatusOr<std::uint64_t> bytes = ParseBlockSize(*spec);
  if (!bytes.ok()) {
    return bytes.status();
  }
  *block_size = *bytes;
  return absl::OkStatus();
}

// --regextype=RE2|EXACT|PCRE2: validates the grammar selector for the whole run. The grammar itself
// is resolved by the parser (parser::GrammarFromGlobals) and pre-compiled into each matcher; this is
// the single validating reader, called unconditionally so it guards every pattern predicate
// (-regex/-rxc/-grep). RE2 (default) and EXACT (literal) are core engines, always available. PCRE2
// is a build-time extra: when its backend is not linked it is a usage error here, never a silent RE2
// fallback. MATCH is still reserved. An unknown value is a usage error. All are refused before the
// walk (exit 2). Last occurrence wins (the parser agrees).
absl::Status ValidateRegextype(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--regextype=";
  for (const std::string& global : globals) {
    if (!global.starts_with(kPrefix)) {
      continue;
    }
    const std::string_view value = std::string_view(global).substr(kPrefix.size());
    if (value == "RE2" || value == "EXACT" || value == "FNMATCH" || value == "GLOB" || value == "SHGLOB") {
      continue;  // core engines, always linked
    }
    if (value == "PCRE2") {
      if (!regex::Pcre2Available()) {
        return absl::InvalidArgumentError(
            "--regextype=PCRE2 is not built into this binary (the PCRE2 backend is a build extra)");
      }
    } else if (value == "MATCH") {
      return absl::InvalidArgumentError(
          absl::StrCat(
              "--regextype=", value,
              " is reserved and not supported yet; use RE2, EXACT, FNMATCH, GLOB, SHGLOB or PCRE2"));
    } else {
      return absl::InvalidArgumentError(
          absl::StrCat("unknown --regextype '", value, "'; expected RE2, EXACT, FNMATCH, GLOB, SHGLOB or PCRE2"));
    }
  }
  return absl::OkStatus();
}

// --time-format=NAME sets the default format for a time field rendered without an
// explicit {:qualifier} (a datetime preset name or a custom absl::FormatTime
// pattern). Last occurrence wins; empty (absent) keeps the built-in "space"
// default. Any value is accepted verbatim (an unknown name renders literally,
// like printf), so there is nothing to reject.
std::string ResolveTimeFormat(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--time-format=";
  std::string format;
  for (const std::string& global : globals) {
    if (global.starts_with(kPrefix)) {
      format = global.substr(kPrefix.size());  // last occurrence wins
    }
  }
  return format;
}

// Wraps a FileSystem so Remove previews (emits the path) instead of deleting:
// backs --dry-run for -delete. ReadDir/Stat pass through unchanged.
class DryRunFileSystem : public vfs::FileSystem {
 public:
  DryRunFileSystem(const vfs::FileSystem& fs, EmitFn preview) : fs_(fs), preview_(preview) {}

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view dir) const override { return fs_.ReadDir(dir); }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool follow) const override {
    return fs_.Stat(path, follow);
  }

  bool Access(std::string_view path, vfs::AccessMode mode) const override { return fs_.Access(path, mode); }

  absl::StatusOr<std::string> ReadLink(std::string_view path) const override { return fs_.ReadLink(path); }

  absl::StatusOr<std::string> FsType(std::string_view path) const override { return fs_.FsType(path); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view path) const override { return fs_.IsCaseSensitive(path); }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override { return fs_.ReadContent(path); }

  absl::Status Remove(std::string_view path) const override {
    preview_(absl::StrCat(path, "\n"));  // would-delete preview; nothing is removed
    return absl::OkStatus();
  }

 private:
  const vfs::FileSystem& fs_;
  EmitFn preview_;
};

// True if the expression contains an armed (effectful) action -- -delete or
// -exec. --safe refuses these. (-delete additionally implies -depth, applied
// in ScanDepthOptions.)
bool ContainsArmedAction(const parser::Expr& expr) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: return expr.descriptor->name == "-delete" || expr.descriptor->name == "-exec";
    case parser::Expr::Kind::kNot: return ContainsArmedAction(*expr.lhs);
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXor:
    case parser::Expr::Kind::kXnor:
    case parser::Expr::Kind::kComma: return ContainsArmedAction(*expr.lhs) || ContainsArmedAction(*expr.rhs);
  }
  return false;
}

// True if the expression mentions the primary `name` anywhere. Used for the
// positional options that take effect run-wide regardless of position (-daystart).
bool ContainsPrimary(const parser::Expr& expr, std::string_view name) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: return expr.descriptor->name == name;
    case parser::Expr::Kind::kNot: return ContainsPrimary(*expr.lhs, name);
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXor:
    case parser::Expr::Kind::kXnor:
    case parser::Expr::Kind::kComma: return ContainsPrimary(*expr.lhs, name) || ContainsPrimary(*expr.rhs, name);
  }
  return false;
}

// The path of `path` relative to the search `root` it was reached from, '/'-
// separated with no leading '/' -- what the ignore patterns match against. The
// walk only yields the root itself or paths beneath it, so stripping the root
// prefix (and any separator) is exact; the root entry maps to "" (never ignored).
std::string_view RelativeTo(std::string_view path, std::string_view root) {
  if (path == root || root.empty()) {
    return path == root ? std::string_view() : path;
  }
  if (path.size() > root.size() && path.substr(0, root.size()) == root) {
    std::string_view rest = path.substr(root.size());
    while (!rest.empty() && rest.front() == '/') {
      rest.remove_prefix(1);
    }
    return rest;
  }
  return path;  // not root-prefixed (should not happen from the walk); match whole
}

// Builds the ignore/filter set from the run's `--exclude=GLOB` / `--include=GLOB`
// globals, in command-line order so the gitignore last-match-wins rule holds across
// them: `--exclude` adds a plain pattern, `--include` a negation (re-include).
ignore::PatternList BuildIgnorePatterns(const std::vector<std::string>& globals) {
  constexpr std::string_view kExclude = "--exclude=";
  constexpr std::string_view kInclude = "--include=";
  ignore::PatternList patterns;
  for (const std::string& global : globals) {
    if (global.starts_with(kExclude)) {
      patterns.Add(global.substr(kExclude.size()), /*negate=*/false);
    } else if (global.starts_with(kInclude)) {
      patterns.Add(global.substr(kInclude.size()), /*negate=*/true);
    }
  }
  return patterns;
}

// Resolves a search root to an absolute, normalized path for repo discovery (which
// walks the path string upward, so it needs an absolute path). Prepends the process
// cwd for a relative root; falls back to the raw path if that fails.
std::string AbsoluteDir(std::string_view root) {
  std::error_code ec;
  const std::filesystem::path abs = std::filesystem::absolute(std::filesystem::path(root), ec);
  return ec ? std::string(root) : abs.lexically_normal().string();
}

// Per-directory ignore-file lookup for --ignore-files / -g: reads and caches each
// directory's combined ignore PatternList (.gitignore < .ignore < .xffignore, so the
// later file wins) lazily, and answers the ignore Decision for a path from the ignore
// files in its ancestor directories -- deepest first, so a deeper file overrides a
// shallower one (gitignore precedence).
//
// When gitignore is on and a search root is inside a git repo, the ancestor walk is
// rebased on the REPO ROOT (git/rg/fd behavior): every directory from the entry up to
// the repo root is consulted -- including directories ABOVE the search root -- and the
// repo's `.git/info/exclude` is applied at the bottom (below every `.gitignore`). Off
// a repo (or gitignore off) the walk stops at the search root, unchanged.
//
// Single-threaded: the walk visitor runs on one coordinator thread, so the cache needs
// no lock. Inactive (no filenames) is a no-op.
class IgnoreFileCache {
 public:
  IgnoreFileCache(
      const vfs::FileSystem& fs,
      std::vector<std::string> filenames,
      bool gitignore_on,
      ignore::PatternList global_excludes)
      : fs_(&fs),
        filenames_(std::move(filenames)),
        gitignore_on_(gitignore_on),
        global_excludes_(std::move(global_excludes)) {}

  bool active() const { return !filenames_.empty(); }

  ignore::Decision Decide(std::string_view path, std::string_view root, bool is_dir) {
    const Scope& scope = ScopeFor(root);
    // In repo scope the walk is done in absolute paths anchored at the repo root, so a
    // relative entry (e.g. under root ".") is first rebased onto the search root's
    // absolute form. Off a repo, `base` is the search root as given and no rebase is
    // needed. `owner` keeps the rebased string alive for the string_views below.
    std::string owner;
    std::string_view rel;
    if (scope.in_repo) {
      const std::string_view rel_to_root = RelativeTo(path, root);
      owner = rel_to_root.empty() ? scope.abs_root : absl::StrCat(scope.abs_root, "/", rel_to_root);
      rel = RelativeTo(owner, scope.base);
    } else {
      rel = RelativeTo(path, root);
    }
    // A `.gitkeep` intentionally keeps its (otherwise-empty) directory in the repo, so the gitignore
    // layers never ignore it - it is always kept, as if by a top-precedence `!.gitkeep`. Explicit
    // --exclude / --include (global_excludes_) still decide, so a user can still override it.
    if (active() && rel.substr(rel.rfind('/') + 1) == ".gitkeep") {
      return global_excludes_.Match(rel, is_dir);
    }
    // Walk the ancestor directories of the entry, deepest first: for rel "a/b/c" that is
    // "a/b", then "a", then "" (the base). Each directory's ignore file matches the entry
    // relative to THAT directory.
    std::string_view ancestor = rel;
    for (;;) {
      const std::string_view::size_type slash = ancestor.rfind('/');
      ancestor = slash == std::string_view::npos ? std::string_view() : ancestor.substr(0, slash);
      const std::string dir = ancestor.empty() ? scope.base : absl::StrCat(scope.base, "/", ancestor);
      const std::string_view sub = ancestor.empty() ? rel : rel.substr(ancestor.size() + 1);
      const ignore::Decision decision = ListFor(dir).Match(sub, is_dir);
      if (decision != ignore::Decision::kDefault) {
        return decision;
      }
      if (ancestor.empty()) {
        // At the repo root, `.git/info/exclude` applies below every `.gitignore`, and
        // git's global excludes (`core.excludesFile`) below that -- both repo-root-
        // anchored, so they match the entry relative to the repo root: `rel`.
        if (scope.in_repo) {
          const ignore::Decision decision = RepoExcludeFor(scope.base).Match(rel, is_dir);
          if (decision != ignore::Decision::kDefault) {
            return decision;
          }
        }
        return global_excludes_.Match(rel, is_dir);  // lowest layer; empty (a no-op) when off / none
      }
    }
  }

 private:
  // Where a search root's ignore walk is anchored: `base` is the repo root (absolute)
  // when gitignore is on and the root is in a repo (`in_repo`), else the search root as
  // given. `abs_root` is the root's absolute form, used to rebase relative entries.
  struct Scope {
    std::string base;
    std::string abs_root;
    bool in_repo = false;
  };

  const Scope& ScopeFor(std::string_view root) {
    const auto it = scope_cache_.find(std::string(root));
    if (it != scope_cache_.end()) {
      return it->second;
    }
    Scope scope;
    if (gitignore_on_) {
      scope.abs_root = AbsoluteDir(root);
      if (const std::optional<std::string> repo_root = repo::FindRepoRoot(*fs_, scope.abs_root)) {
        scope.base = *repo_root;
        scope.in_repo = true;
      }
    }
    if (!scope.in_repo) {
      scope.base = std::string(root);
    }
    return scope_cache_.emplace(std::string(root), std::move(scope)).first->second;
  }

  const ignore::PatternList& ListFor(const std::string& dir) {
    const auto it = cache_.find(dir);
    if (it != cache_.end()) {
      return it->second;
    }
    ignore::PatternList list;
    for (const std::string& name : filenames_) {
      if (const absl::StatusOr<std::string> content = fs_->ReadContent(absl::StrCat(dir, "/", name)); content.ok()) {
        list.AddPatterns(*content);
      }
    }
    return cache_.emplace(dir, std::move(list)).first->second;
  }

  // The repo's `.git/info/exclude` (gitignore-format), matched relative to the repo
  // root; cached per repo root, empty when the file is absent.
  const ignore::PatternList& RepoExcludeFor(const std::string& repo_root) {
    const auto it = repo_exclude_cache_.find(repo_root);
    if (it != repo_exclude_cache_.end()) {
      return it->second;
    }
    ignore::PatternList list;
    if (const absl::StatusOr<std::string> content = fs_->ReadContent(absl::StrCat(repo_root, "/.git/info/exclude"));
        content.ok()) {
      list.AddPatterns(*content);
    }
    return repo_exclude_cache_.emplace(repo_root, std::move(list)).first->second;
  }

  const vfs::FileSystem* fs_;
  std::vector<std::string> filenames_;
  bool gitignore_on_;
  ignore::PatternList global_excludes_;  // git core.excludesFile, applied below .git/info/exclude
  std::map<std::string, Scope> scope_cache_;
  std::map<std::string, ignore::PatternList> cache_;
  std::map<std::string, ignore::PatternList> repo_exclude_cache_;
};

// --ignore-file=PATH sources: each an explicitly named ignore file whose gitignore-format
// patterns are rooted at the file's OWN directory (its absolute, normalized parent), not the
// search root. An entry matches a source only when it lies within that root; its path relative
// to the root is tested. Pointing the flag at the file both selects the patterns and anchors
// them, so no separate --ignore-file-root flag is needed. Repeatable: sources are consulted in
// command-line order and the last non-silent one wins (later --ignore-file overrides earlier,
// like the gitignore last-match convention across files). Read best-effort -- an unreadable file
// contributes nothing, matching how a missing .gitignore is handled. Applied whole-run, so it is
// independent of -g / --gitignore (which drive the auto per-directory stack).
class RootedIgnoreFiles {
 public:
  RootedIgnoreFiles() = default;

  // Builds the sources from the --ignore-file=PATH globals (command-line order), reading each
  // through `fs`. A source's root is AbsoluteDir(dirname(PATH)); a bare filename roots at cwd.
  static RootedIgnoreFiles FromGlobals(const vfs::FileSystem& fs, const std::vector<std::string>& globals) {
    constexpr std::string_view kIgnoreFile = "--ignore-file=";
    RootedIgnoreFiles out;
    for (const std::string& global : globals) {
      if (!global.starts_with(kIgnoreFile)) {
        continue;
      }
      const std::string_view path = std::string_view(global).substr(kIgnoreFile.size());
      if (path.empty()) {
        continue;
      }
      const std::string_view::size_type slash = path.rfind('/');
      const std::string_view dir = slash == std::string_view::npos ? std::string_view(".") : path.substr(0, slash);
      Source source{.root = AbsoluteDir(dir), .patterns = {}};
      if (const absl::StatusOr<std::string> content = fs.ReadContent(path); content.ok()) {
        source.patterns = ignore::PatternList::Parse(*content);
      }
      out.sources_.push_back(std::move(source));
    }
    return out;
  }

  bool active() const { return !sources_.empty(); }

  // The decision for an entry given its absolute, normalized path. Each source whose root
  // contains the entry contributes; the last non-default decision wins (later --ignore-file
  // overrides earlier). kDefault when no source's root contains the entry, or all are silent.
  ignore::Decision Decide(std::string_view abs_path, bool is_dir) const {
    ignore::Decision result = ignore::Decision::kDefault;
    for (const Source& source : sources_) {
      const std::optional<std::string_view> rel = Under(abs_path, source.root);
      if (!rel.has_value()) {
        continue;  // the entry is outside this source's root subtree
      }
      if (const ignore::Decision decision = source.patterns.Match(*rel, is_dir);
          decision != ignore::Decision::kDefault) {
        result = decision;
      }
    }
    return result;
  }

 private:
  struct Source {
    std::string root;  // absolute, normalized directory the patterns are relative to
    ignore::PatternList patterns;
  };

  // `abs_path` relative to `root` ('/'-separated, no leading '/'), or nullopt when `abs_path` is
  // neither `root` nor beneath it. The root directory itself maps to "" (matched against, but a
  // pattern rarely matches "", and the walk never filters the named search root anyway).
  static std::optional<std::string_view> Under(std::string_view abs_path, std::string_view root) {
    if (abs_path == root) {
      return std::string_view();
    }
    if (abs_path.size() > root.size() && abs_path.substr(0, root.size()) == root && abs_path[root.size()] == '/') {
      return abs_path.substr(root.size() + 1);
    }
    return std::nullopt;
  }

  std::vector<Source> sources_;
};

bool HasGlobal(const std::vector<std::string>& globals, std::string_view flag) {
  for (const std::string& global : globals) {
    if (global == flag) {
      return true;
    }
  }
  return false;
}

// --gitignore / -g ternary. Bare `-g` / `--gitignore` selects AUTO (respect .gitignore
// only when the traversal is inside a git repo, matching git's own behavior);
// `--gitignore=on` (or the short `-g+`) forces it on regardless, `--gitignore=off`
// (short `-g-`) forces it off. Last occurrence wins. Off by default (find-compatible).
// -u / --no-ignore overrules them all: the master switch over every ignore source is
// position-independent, not a participant in the last-wins scan.
enum class GitignoreMode { kOff, kOn, kAuto };

GitignoreMode ResolveGitignoreMode(const std::vector<std::string>& globals, std::optional<registry::Style> style) {
  if (HasGlobal(globals, "--no-ignore") || HasGlobal(globals, "-u")) {
    return GitignoreMode::kOff;
  }
  // The opinionated style (rg) respect ignore files by default (their headline
  // behavior); find/xff start off (find-compatible). An explicit -g / --gitignore flag
  // still overrides.
  const bool opinionated = style == registry::Style::kRg;
  GitignoreMode mode = opinionated ? GitignoreMode::kOn : GitignoreMode::kOff;
  for (const std::string& global : globals) {
    if (global == "-g" || global == "--gitignore") {
      mode = GitignoreMode::kAuto;
    } else if (global == "-g+" || global == "--gitignore=on") {
      mode = GitignoreMode::kOn;
    } else if (global == "-g-" || global == "--gitignore=off") {
      mode = GitignoreMode::kOff;
    }
  }
  return mode;
}

// --hidden / --no-hidden: whether to skip hidden dotfiles (a path component starting with
// '.'). Default is style-scoped: find and the conservative xff style show them
// (find-compatible), the opinionated style (rg) skips them (fd-like, less dotclutter).
// --hidden forces show, --no-hidden forces skip; last occurrence wins. An explicitly named
// search root is always entered regardless (handled at the walk by depth).
bool ResolveSkipHidden(const std::vector<std::string>& globals, std::optional<registry::Style> style) {
  bool skip = style == registry::Style::kRg;
  for (const std::string& global : globals) {
    if (global == "--hidden") {
      skip = false;
    } else if (global == "--no-hidden") {
      skip = true;
    }
  }
  return skip;
}

// The version-control systems whose metadata --skip-vcs can prune: a token (the --skip-vcs= value)
// mapped to the directory / gitlink-file name to drop. Order matches the --help / display order.
constexpr std::array<std::pair<std::string_view, std::string_view>, 7> kVcsMetadata = {{
    {"git", ".git"},
    {"hg", ".hg"},
    {"svn", ".svn"},
    {"jj", ".jj"},
    {"bzr", ".bzr"},
    {"darcs", "_darcs"},
    {"cvs", "CVS"},
}};

// Resolves the VCS metadata NAMES (`.git`, `.hg`, ...) --skip-vcs should prune from the walk.
// Explicit --skip-vcs / --no-skip-vcs win, last occurrence: bare or `=all` selects every VCS, `=none`
// (or --no-skip-vcs) selects none, a comma list selects exactly those tokens (a frozen subset). With
// no such flag, gitignore mode (`-g`) implies just `.git` (the shipped default), else nothing. An
// unknown token is an InvalidArgument usage error, refused before the walk.
absl::StatusOr<absl::flat_hash_set<std::string>> ResolveSkipVcs(
    const std::vector<std::string>& globals,
    bool gitignore_on) {
  constexpr std::string_view kPrefix = "--skip-vcs=";
  const auto dir_for = [](std::string_view token) -> std::optional<std::string_view> {
    for (const auto& [tok, dir] : kVcsMetadata) {
      if (tok == token) {
        return dir;
      }
    }
    return std::nullopt;
  };
  const auto add_all = [](absl::flat_hash_set<std::string>& names) {
    for (const auto& [tok, dir] : kVcsMetadata) {
      names.emplace(dir);
    }
  };
  bool saw_flag = false;
  absl::flat_hash_set<std::string> names;
  for (const std::string& global : globals) {
    if (global == "--no-skip-vcs") {
      saw_flag = true;
      names.clear();
    } else if (global == "--skip-vcs") {
      saw_flag = true;
      names.clear();
      add_all(names);
    } else if (global.starts_with(kPrefix)) {
      saw_flag = true;
      names.clear();
      const std::string_view value = std::string_view(global).substr(kPrefix.size());
      if (value == "all") {
        add_all(names);
      } else if (value != "none") {
        for (const std::string_view token : absl::StrSplit(value, ',', absl::SkipEmpty())) {
          const std::optional<std::string_view> dir = dir_for(token);
          if (!dir.has_value()) {
            return absl::InvalidArgumentError(
                absl::StrCat(
                    "unknown --skip-vcs value '", token,
                    "'; expected a comma list of git,hg,svn,jj,bzr,darcs,cvs (or all/none)"));
          }
          names.emplace(*dir);
        }
      }
    }
  }
  if (!saw_flag && gitignore_on) {
    names.emplace(".git");  // -g implies --skip-vcs=git (the shipped default)
  }
  return names;
}

// -g auto: whether any search root is inside a git working tree, so .gitignore applies.
bool AnyRootInRepo(const vfs::FileSystem& fs, const std::vector<std::string>& roots) {
  return absl::c_any_of(
      roots, [&fs](std::string_view root) { return repo::FindRepoRoot(fs, AbsoluteDir(root)).has_value(); });
}

// The per-directory ignore filenames in effect, lowest precedence first (a directory's
// files accumulate into one list, so a later name wins): .gitignore (-g) < .ignore <
// .xffignore (--ignore-files). Empty when ignore-file processing is off -- it is
// find-compatibly off by default, and --no-ignore / -u is the master switch that
// force-disables every source.
std::vector<std::string> ResolveIgnoreFileNames(
    const std::vector<std::string>& globals,
    bool gitignore_on,
    std::optional<registry::Style> style) {
  if (HasGlobal(globals, "--no-ignore") || HasGlobal(globals, "-u")) {
    return {};
  }
  std::vector<std::string> names;
  if (gitignore_on) {
    names.emplace_back(".gitignore");
  }
  // The opinionated style (rg) also honor .ignore / .xffignore by default (like
  // ripgrep / fd); other styles need --ignore-files. -u / --no-ignore above still
  // force-disables all.
  const bool opinionated = style == registry::Style::kRg;
  if (HasGlobal(globals, "--ignore-files") || opinionated) {
    names.emplace_back(".ignore");
    names.emplace_back(".xffignore");
  }
  return names;
}

// --implicit-print=yes|no forces the default (implicit) print on or off,
// overriding find's "an action suppresses it" rule. Last occurrence wins;
// nullopt means no override (use the find default). Bare --implicit-print == =yes.
std::optional<bool> ResolveImplicitPrint(const std::vector<std::string>& globals) {
  std::optional<bool> result;
  for (const std::string& global : globals) {
    if (global == "--implicit-print" || global == "--implicit-print=yes") {
      result = true;
    } else if (global == "--implicit-print=no") {
      result = false;
    }
  }
  return result;
}

// Collects --define=NAME=VALUE globals into a name->value map (last wins). The
// text after the prefix is NAME=VALUE; NAME runs to the first '=', VALUE (which
// may itself contain '=') is the rest. --define=NAME with no '=' binds empty.
std::map<std::string, std::string> ResolveDefines(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--define=";
  std::map<std::string, std::string> defines;
  for (const std::string& global : globals) {
    if (!global.starts_with(kPrefix)) {
      continue;
    }
    const std::string spec = global.substr(kPrefix.size());
    const std::string::size_type eq = spec.find('=');
    if (eq == std::string::npos) {
      defines[spec] = "";
    } else {
      defines[spec.substr(0, eq)] = spec.substr(eq + 1);
    }
  }
  return defines;
}

// Whether --capture-override permits re-binding a -capture NAME. Strict by
// default (a duplicate name is an error); --capture-override (== =yes) allows it,
// --capture-override=no restores strict. Last occurrence wins.
bool CaptureOverride(const std::vector<std::string>& globals) {
  bool allow = false;
  for (const std::string& global : globals) {
    if (global == "--capture-override" || global == "--capture-override=yes") {
      allow = true;
    } else if (global == "--capture-override=no") {
      allow = false;
    }
  }
  return allow;
}

// Collects the NAME of every -capture action in the expression (its args[0]).
void CollectCaptureNames(const parser::Expr& expr, std::vector<std::string>* names) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate:
      if (expr.descriptor->name == "-capture" && !expr.args.empty()) {
        names->push_back(expr.args.front());
      }
      break;
    case parser::Expr::Kind::kNot: CollectCaptureNames(*expr.lhs, names); break;
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXor:
    case parser::Expr::Kind::kXnor:
    case parser::Expr::Kind::kComma:
      CollectCaptureNames(*expr.lhs, names);
      CollectCaptureNames(*expr.rhs, names);
      break;
  }
}

// Returns a -capture NAME bound more than once, or nullopt when all are unique.
std::optional<std::string> DuplicateCaptureName(const parser::Expr& expr) {
  std::vector<std::string> names;
  CollectCaptureNames(expr, &names);
  std::sort(names.begin(), names.end());
  const auto dup = std::adjacent_find(names.begin(), names.end());
  return dup == names.end() ? std::nullopt : std::optional<std::string>(*dup);
}

// Collects strings that may reference {capture.NAME}: the command tokens of every
// -exec and -capture action (a later command can use an earlier capture). The
// --template global is added by the caller.
void CollectCaptureRefs(const parser::Expr& expr, std::vector<std::string>* refs) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate:
      if (expr.descriptor->name == "-exec") {
        refs->insert(refs->end(), expr.args.begin(), expr.args.end());
      } else if (expr.descriptor->name == "-capture" && expr.args.size() > 2) {
        refs->insert(refs->end(), expr.args.begin() + 2, expr.args.end());  // skip [NAME, REGEX]
      }
      break;
    case parser::Expr::Kind::kNot: CollectCaptureRefs(*expr.lhs, refs); break;
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXor:
    case parser::Expr::Kind::kXnor:
    case parser::Expr::Kind::kComma:
      CollectCaptureRefs(*expr.lhs, refs);
      CollectCaptureRefs(*expr.rhs, refs);
      break;
  }
}

// Returns a -capture NAME whose {capture.NAME} placeholder appears nowhere (no
// -exec/-capture command, not the --template, and not a --summary={...} key), or nullopt when all
// are used.
std::optional<std::string> UnusedCaptureName(
    const parser::Expr& expr,
    const std::optional<std::string>& tmpl,
    std::string_view summary_key) {
  std::vector<std::string> names;
  CollectCaptureNames(expr, &names);
  if (names.empty()) {
    return std::nullopt;
  }
  std::vector<std::string> refs;
  CollectCaptureRefs(expr, &refs);
  if (tmpl.has_value()) {
    refs.push_back(*tmpl);
  }
  if (!summary_key.empty()) {
    refs.emplace_back(summary_key);  // --summary={...} references captures too
  }
  for (const std::string& name : names) {
    const std::string closed = absl::StrCat("{capture.", name, "}");
    const std::string qualified = absl::StrCat("{capture.", name, ":");
    const bool used = std::any_of(refs.begin(), refs.end(), [&](const std::string& ref) {
      return ref.find(closed) != std::string::npos || ref.find(qualified) != std::string::npos;
    });
    if (!used) {
      return name;
    }
  }
  return std::nullopt;
}

}  // namespace

// --human[=iec|si|off] (and the --si alias): how sizes render in -ls and --summary. si = decimal
// (kB/MB, 1000^N) - the default, since it reads most human; iec = binary (KiB/MiB, 1024^N); off =
// raw bytes. Numeric synonyms: 1000 = si, 1024 = iec. Bare --human and --si both select si. The
// default when unset is style-scoped: the modern styles (xff, rg) show human (si), the find style
// shows raw bytes (find -ls compatibility). Last occurrence wins; nullopt means raw bytes.
std::optional<format::SizeUnits> ResolveHuman(
    const std::vector<std::string>& globals,
    std::optional<registry::Style> style) {
  std::optional<format::SizeUnits> units;
  if (style.has_value() && *style != registry::Style::kFind) {
    units = format::SizeUnits::kSi;  // the modern styles (xff, rg) default to human sizes (SI)
  }
  for (std::string_view global : globals) {
    if (global == "--human" || global == "--human=si" || global == "--human=1000" || global == "--si") {
      units = format::SizeUnits::kSi;  // bare --human defaults to SI (KB/MB); --si is its alias
    } else if (global == "--human=iec" || global == "--human=1024") {
      units = format::SizeUnits::kIec;
    } else if (global == "--human=off") {
      units = std::nullopt;  // force raw bytes, even in the modern styles
    }
  }
  return units;
}

// The resolved --buffer cap for a column buffer: a row `window` and/or a `byte_budget` (0 =
// no byte cap). A byte-budget value clears the row cap to kAll so only the bytes bound.
struct BufferBound {
  std::size_t window;
  std::size_t byte_budget;
};

// --buffer=auto|off|all|N[k/M/G]|N<byte-unit>: how much to buffer to align columns (-ls, and
// the buffered --format=aligned/markdown tables). auto (=100) buffers the first 100 rows to
// compute widths then streams the rest at them; off / 0 disables buffering; all buffers the
// whole run; a row count N (optional decimal SI multiplier k/M/G/T) buffers N rows; a byte
// budget (N with a byte unit, e.g. 10MB / 10MiB) buffers until that many cell bytes, then
// streams. Last occurrence wins; an unrecognized value is ignored. `default_window` applies
// when no --buffer flag is present (-ls passes 100 = auto; the tables pass kAll = full align).
BufferBound ResolveBufferBound(const std::vector<std::string>& globals, std::size_t default_window) {
  BufferBound bound{.window = default_window, .byte_budget = 0};
  for (const std::string& global : globals) {
    if (global == "--buffer") {
      bound = {.window = 100, .byte_budget = 0};  // bare --buffer == --buffer=auto
    } else if (global.starts_with("--buffer=")) {
      const std::string_view value = std::string_view(global).substr(9);
      if (const std::optional<std::size_t> rows = format::ParseBufferWindow(value)) {
        bound = {.window = *rows, .byte_budget = 0};
      } else if (const std::optional<std::size_t> bytes = format::ParseByteBudget(value)) {
        bound = {.window = format::ColumnBuffer::kAll, .byte_budget = *bytes};  // bytes-only cap
      }
      // else: unrecognized value -> keep the previous bound
    }
  }
  return bound;
}

// --top=N: with --summary, keep only the N largest groups by size. Last occurrence
// wins; nullopt (absent, or a non-positive / malformed N) means no limit -- all
// groups in the default alphabetical order.
std::optional<std::size_t> ResolveTop(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--top=";
  std::optional<std::size_t> top;
  for (const std::string& global : globals) {
    if (!global.starts_with(kPrefix)) {
      continue;
    }
    if (std::size_t value = 0; absl::SimpleAtoi(std::string_view(global).substr(kPrefix.size()), &value) && value > 0) {
      top = value;
    }
  }
  return top;
}

// --histogram-width=N: the cell width the tallest histogram bar fills (default 40). A non-positive
// or malformed value is ignored (keeps the default); the last valid one wins.
std::size_t ResolveHistogramWidth(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--histogram-width=";
  std::size_t width = 40;
  for (const std::string& global : globals) {
    if (!global.starts_with(kPrefix)) {
      continue;
    }
    if (std::size_t value = 0; absl::SimpleAtoi(std::string_view(global).substr(kPrefix.size()), &value) && value > 0) {
      width = value;
    }
  }
  return width;
}

// --summary-precision=N: fraction digits for the --summary human size column (default 2,
// e.g. "12.34 MiB"). A malformed value keeps the default; the count is capped at 9 so the
// column stays readable. Bytes stay integer regardless (12 B), with the fraction columns
// blanked so points line up (see format::SizeColumns).
unsigned ResolveSummaryPrecision(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--summary-precision=";
  unsigned precision = 2;
  for (const std::string& global : globals) {
    if (!global.starts_with(kPrefix)) {
      continue;
    }
    if (std::uint32_t value = 0;
        absl::SimpleAtoi(std::string_view(global).substr(kPrefix.size()), &value) && value <= 9) {
      precision = value;
    }
  }
  return precision;
}

// A JSON string literal for `text` (quotes included): escapes the JSON-significant
// characters and any control byte as \uXXXX. Used for the --summary=jsonl group key
// (type/extension names, which are normally plain but may carry odd bytes).
std::string JsonQuote(std::string_view text) {
  std::string out = "\"";
  for (const char ch : text) {
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          absl::StrAppend(&out, "\\u", absl::Hex(static_cast<unsigned char>(ch), absl::kZeroPad4));
        } else {
          out.push_back(ch);
        }
    }
  }
  out.push_back('"');
  return out;
}

int RunFind(
    const parser::Command& command,
    const vfs::FileSystem& fs,
    EmitFn emit,
    WalkErrorFn on_error,
    std::optional<registry::Style> style,
    bool* any_match) {
  if (any_match != nullptr) {
    *any_match = false;  // no match until an entry satisfies the expression
  }
  const parser::Expr* const expression = command.expression.get();
  const bool has_action = expression != nullptr && ContainsAction(*expression);
  // --implicit-print=yes|no overrides find's default-print rule (otherwise !has_action).
  const bool implicit_print = ResolveImplicitPrint(command.globals).value_or(!has_action);
  if (HasGlobal(command.globals, "--safe") && expression != nullptr && ContainsArmedAction(*expression)) {
    on_error("-delete", absl::FailedPreconditionError("refused: --safe forbids destructive actions"));
    return 2;  // do not traverse
  }
  // A -capture NAME bound twice is an error by default (silent clobbering would
  // mean silently-wrong data); --capture-override opts into last-wins.
  if (expression != nullptr && !CaptureOverride(command.globals)) {
    if (const std::optional<std::string> dup = DuplicateCaptureName(*expression); dup.has_value()) {
      on_error(
          "-capture",
          absl::FailedPreconditionError(absl::StrCat("duplicate -capture name '", *dup, "'; use --capture-override")));
      return 2;  // do not traverse
    }
  }
  WalkOptions options;
  options.symlinks = ResolveSymlinkMode(command.globals);
  options.sort = ResolveSort(command.globals, style);
  options.workers = ResolveJobs(command.globals, style);
  const render::Format format = ResolveFormat(command.globals);
  const render::PathEncoding path_encoding = ResolvePathEncoding(command.globals);
  // --color=auto|always|never: colorize the plain listing by file type. auto (the
  // default) colors only a real terminal with NO_COLOR unset; a pipe (or a test's
  // captured stdout) is not a tty, so it stays plain unless --color=always forces it.
  const bool colorize =
      format == render::Format::kPlain
      && color::Enabled(
          color::ResolveWhen(command.globals), ::isatty(STDOUT_FILENO) != 0, std::getenv("NO_COLOR") != nullptr);
  const std::optional<std::string> tmpl = ResolveTemplate(command.globals);
  // A -capture whose {capture.NAME} is never referenced ran a subprocess for
  // nothing (use -exec for pure side effects); flag it before traversing.
  if (expression != nullptr) {
    if (const std::optional<std::string> unused =
            UnusedCaptureName(*expression, tmpl, SummaryKeyTemplate(command.globals));
        unused.has_value()) {
      on_error(
          "-capture", absl::FailedPreconditionError(
                          absl::StrCat("-capture '", *unused, "' is never referenced as {capture.", *unused, "}")));
      return 2;  // do not traverse
    }
  }
  // Precompile the --template once; rendering each match then skips re-scanning.
  const std::optional<fields::Template> compiled_tmpl =
      tmpl.has_value() ? std::optional<fields::Template>(fields::Template::Compile(*tmpl)) : std::nullopt;
  // --columns=FIELD,... : the tabular column set (--format=csv/tsv/aligned/markdown). Validate
  // before the walk -- an unknown column, a non-tabular format, or a suppressed default listing
  // (an action / --implicit-print=no) is a usage error, not a silently-empty or moot table.
  // aligned/markdown are `buffered` (the whole table renders after the walk, once every column
  // width is known); csv/tsv stream a row at a time. All four support --columns + a header.
  const std::vector<std::string> columns = ResolveColumns(command.globals);
  const bool buffered = format == render::Format::kAligned || format == render::Format::kMarkdown;
  const bool is_tree = format == render::Format::kTree;
  const bool tabular = format == render::Format::kCsv || format == render::Format::kTsv || buffered;
  if ((tabular || is_tree || !columns.empty()) && !implicit_print) {
    on_error(
        "--format", absl::FailedPreconditionError(
                        "tabular/tree output (--format=csv/tsv/aligned/markdown/tree) and --columns format the "
                        "default listing; an action like -ls / -printf / -exec produces its own output -- drop "
                        "the action, or drop --format / --columns"));
    return 2;
  }
  if (!columns.empty() && !tabular) {
    on_error(
        "--columns",
        absl::FailedPreconditionError("--columns needs a tabular --format (csv, tsv, aligned, or markdown)"));
    return 2;
  }
  for (const std::string& col : columns) {
    if (col.empty() || !fields::IsKnownField(col)) {
      on_error("--columns", absl::InvalidArgumentError(absl::StrCat("unknown column '", col, "'")));
      return 2;
    }
  }
  // Precompile one field Template per column ({col}); each match renders them into a row.
  std::vector<fields::Template> column_templates;
  column_templates.reserve(columns.size());
  for (const std::string& col : columns) {
    column_templates.push_back(fields::Template::Compile(absl::StrCat("{", col, "}")));
  }
  const bool exec_fields = HasGlobal(command.globals, "--exec-fields");  // route -exec through the vocabulary
  const std::map<std::string, std::string> defines = ResolveDefines(command.globals);  // {def.NAME} values
  // --exclude=GLOB / --include=GLOB: a run-level gitignore-style filter. An ignored
  // entry is dropped before evaluation (a matched directory is pruned, not descended);
  // --include re-includes. Empty when neither flag is present (zero overhead).
  const ignore::PatternList ignore_patterns = BuildIgnorePatterns(command.globals);
  if (expression != nullptr) {
    ScanDepthOptions(*expression, &options);
  }
  // A malformed -size / -blocks value (unknown unit, an over-64-bit unit like Z/Y,
  // or a non-numeric count) is a usage error refused before the walk -- find rejects
  // bad -size at parse time too, rather than silently matching nothing.
  if (expression != nullptr) {
    if (const absl::Status size_status = ValidateSizeArgs(*expression); !size_status.ok()) {
      on_error("-size/-blocks", size_status);
      return 2;  // do not traverse
    }
  }
  // --timezone=ZONE overrides the local zone for interpreting time-string args
  // (-newerXt) and -daystart's midnight. Resolved first (both need it); an unknown
  // zone is a usage error, refused before traversal.
  absl::TimeZone tz = absl::LocalTimeZone();
  if (std::string bad; !ResolveTimeZone(command.globals, &tz, &bad)) {
    on_error("--timezone", absl::InvalidArgumentError(absl::StrCat("unknown time zone: '", bad, "'")));
    return 2;  // do not traverse
  }
  // Capture one reference instant so every entry's age test (-mtime/-mmin) is
  // measured against the same clock. -daystart measures from today's local
  // midnight (in tz) instead of find's start time (the run's start).
  const bool daystart = expression != nullptr && ContainsPrimary(*expression, "-daystart");
  const absl::Time now = daystart ? datetime::StartOfDay(absl::Now(), tz) : absl::Now();
  // --time-format=NAME: default spec for a time field with no {:qualifier}.
  const std::string time_format = ResolveTimeFormat(command.globals);
  // --block-size=SIZE: bytes per -size block (a bare value / the 'b' suffix); find's
  // historical default is 512. A malformed SIZE is a usage error, refused here.
  std::uint64_t block_size = 512;
  if (const absl::Status size_status = ResolveBlockSize(command.globals, &block_size); !size_status.ok()) {
    on_error("--block-size", size_status);
    return 2;  // do not traverse
  }
  // --regextype=RE2|EXACT|PCRE2: the grammar is resolved by the parser and pre-compiled into each
  // matcher; here we only validate the selector for the whole run. PCRE2 when not built into this
  // binary, MATCH (reserved), and unknown values are usage errors, refused before the walk.
  if (const absl::Status regextype = ValidateRegextype(command.globals); !regextype.ok()) {
    on_error("--regextype", regextype);
    return 2;  // do not traverse
  }
  // --count / -c: -grep emits a per-file matching-line count instead of the lines.
  const bool grep_count = HasGlobal(command.globals, "--count") || HasGlobal(command.globals, "-c");
  // --context / --before-context / --after-context (grep -C/-B/-A): -grep context lines. Validated
  // here so a bad value is a usage error (exit 2) before the walk.
  std::size_t grep_before = 0;
  std::size_t grep_after = 0;
  bool context_seen = false;
  if (const absl::Status status = ResolveGrepContext(command.globals, grep_before, grep_after, context_seen);
      !status.ok()) {
    on_error("--context", status);
    return 2;
  }
  // --diff-algorithm=naive|direct|myers: the engine -diff uses (mbo::diff). Last occurrence
  // wins; empty -> myers (the default). Validated here so a bad value is a usage error (exit 2)
  // before the walk rather than a silent fallback.
  std::string diff_algorithm;
  for (const std::string& global : command.globals) {
    constexpr std::string_view kDiffAlgo = "--diff-algorithm=";
    if (global.starts_with(kDiffAlgo)) {
      diff_algorithm = global.substr(kDiffAlgo.size());
    }
  }
  if (!diff_algorithm.empty() && !mbo::diff::DiffOptions::ParseAlgorithmFlag(diff_algorithm).has_value()) {
    on_error(
        "--diff-algorithm",
        absl::InvalidArgumentError(
            absl::StrCat("unknown diff algorithm '", diff_algorithm, "' (use naive, direct, or myers)")));
    return 2;
  }
  // --diff-ignore=<tokens> / --diff-ignore-matching=REGEX: -diff normalization (mbo::diff). Last
  // occurrence of each wins; empty -> exact. Validated here (shared with the apply path) so a bad
  // token or regex is a usage error (exit 2) before the walk.
  std::string diff_ignore;
  std::string diff_ignore_matching;
  for (const std::string& global : command.globals) {
    constexpr std::string_view kDiffIgnore = "--diff-ignore=";
    constexpr std::string_view kDiffIgnoreMatching = "--diff-ignore-matching=";
    if (global.starts_with(kDiffIgnoreMatching)) {
      diff_ignore_matching = global.substr(kDiffIgnoreMatching.size());
    } else if (global.starts_with(kDiffIgnore)) {
      diff_ignore = global.substr(kDiffIgnore.size());
    }
  }
  if (const absl::Status status = ValidateDiffIgnore(diff_ignore, diff_ignore_matching); !status.ok()) {
    on_error("--diff-ignore", status);
    return 2;
  }
  // --diff-format=u|c|n|y|unified|context|normal|side-by-side: the default -diff output format
  // (last occurrence wins; unset -> unified). A per-action -diff=STYLE letter still overrides it.
  // Validated here so a bad value is a usage error (exit 2) before the walk.
  mbo::diff::DiffOptions::OutputFormat diff_format = mbo::diff::DiffOptions::OutputFormat::kUnified;
  std::string diff_format_flag;
  for (const std::string& global : command.globals) {
    constexpr std::string_view kDiffFormat = "--diff-format=";
    if (global.starts_with(kDiffFormat)) {
      diff_format_flag = global.substr(kDiffFormat.size());
    }
  }
  if (!diff_format_flag.empty()) {
    const std::optional<mbo::diff::DiffOptions::OutputFormat> parsed = ParseDiffFormatFlag(diff_format_flag);
    if (!parsed.has_value()) {
      on_error(
          "--diff-format", absl::InvalidArgumentError(
                               absl::StrCat(
                                   "unknown diff format '", diff_format_flag,
                                   "' (use u/unified, c/context, n/normal, or y/side-by-side)")));
      return 2;
    }
    diff_format = *parsed;
  }
  // --diff-context=N (and --context=N when symmetric): the default -diff context size (built-in 3).
  // --context feeds diff only when before==after (a single symmetric value a diff can represent);
  // --diff-context overrides --context regardless of order; a per-action -diff=uN overrides both.
  std::size_t diff_context = 3;
  if (context_seen && grep_before == grep_after) {
    diff_context = grep_before;
  }
  for (const std::string& global : command.globals) {
    constexpr std::string_view kDiffContext = "--diff-context=";
    if (global.starts_with(kDiffContext)) {
      const std::string_view value = std::string_view(global).substr(kDiffContext.size());
      if (std::size_t n = 0; absl::SimpleAtoi(value, &n)) {
        diff_context = n;
      } else {
        on_error("--diff-context", absl::InvalidArgumentError(absl::StrCat("bad --diff-context value '", value, "'")));
        return 2;
      }
    }
  }
  // --hash-algorithm=ALGO / --hash-encoding=hex|base64: defaults for a bare -hash action and a
  // bare {hash} field (last occurrence wins; empty -> sha256 / hex). Validated here so a bad value
  // is a usage error (exit 2) before the walk; the explicit -hash=ALGO[/ENCODING] specs in the
  // expression are validated by ValidateHashArgs below.
  std::string hash_algorithm;
  std::string hash_encoding;
  for (const std::string& global : command.globals) {
    constexpr std::string_view kHashAlgo = "--hash-algorithm=";
    constexpr std::string_view kHashEncoding = "--hash-encoding=";
    if (global.starts_with(kHashAlgo)) {
      hash_algorithm = global.substr(kHashAlgo.size());
    } else if (global.starts_with(kHashEncoding)) {
      hash_encoding = global.substr(kHashEncoding.size());
    }
  }
  if (!hash_algorithm.empty() && !hash::IsAlgorithm(hash_algorithm)) {
    on_error(
        "--hash-algorithm", absl::InvalidArgumentError(
                                absl::StrCat(
                                    "unknown hash algorithm '", hash_algorithm,
                                    "' (one of: ", absl::StrJoin(hash::AlgorithmNames(), ", "), ")")));
    return 2;
  }
  if (!hash_encoding.empty() && !hash::ParseEncoding(hash_encoding).has_value()) {
    on_error(
        "--hash-encoding",
        absl::InvalidArgumentError(absl::StrCat("unknown hash encoding '", hash_encoding, "' (use hex or base64)")));
    return 2;
  }
  if (expression != nullptr) {
    if (const absl::Status status = ValidateHashArgs(*expression); !status.ok()) {
      on_error("-hash", status);
      return 2;
    }
  }
  // --summary: reduce matches to a {count, total size} per group instead of
  // printing each one; the table is emitted after the walk.
  const SummaryMode summary_mode = ResolveSummary(command.globals);
  // --summary={template}: the group key is a field template, compiled once. An m// extraction key
  // groups per extracted line (a value stream folded into the counts); any other template one key
  // per matched entry. A template mixing an m// extraction with other text has no single key and is
  // a usage error refused before the walk.
  std::optional<fields::Template> summary_template;
  if (summary_mode == SummaryMode::kTemplate) {
    summary_template = fields::Template::Compile(SummaryKeyTemplate(command.globals));
    if (summary_template->HasExtraction() && !summary_template->IsExtraction()) {
      on_error(
          "--summary", absl::InvalidArgumentError(
                           "a --summary key template must be a plain field or exactly one m// extraction, not a mix"));
      return 2;
    }
  }
  const std::optional<format::SizeUnits> human =
      ResolveHuman(command.globals, style);  // --human: size units for --summary and -ls (xff -> human)
  std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> summary;  // group -> {count, total size}
  // --histogram (repeatable): a bar chart of the count per bucket, alongside or instead of
  // --summary. Both are reductions fed by one walk; a run with either suppresses the listing.
  absl::StatusOr<std::vector<HistogramSpec>> histograms_or = ResolveHistograms(command.globals);
  if (!histograms_or.ok()) {
    on_error("--histogram", histograms_or.status());
    return 2;
  }
  const std::vector<HistogramSpec> histograms = *std::move(histograms_or);
  std::vector<std::map<std::string, HistCell>> histogram_cells(histograms.size());  // one per spec
  const bool any_reduction = summary_mode != SummaryMode::kOff || !histograms.empty();
  int errors = 0;

  // --dry-run: route deletions through a previewing wrapper, so -delete reports
  // what it would remove without touching the filesystem.
  const DryRunFileSystem dry_run_fs(fs, emit);
  const vfs::FileSystem& walk_fs = HasGlobal(command.globals, "--dry-run") ? dry_run_fs : fs;
  // --ignore-files: honor per-directory .ignore / .xffignore files (off by default,
  // find-compatible; -u / --no-ignore forces it off). Reads through walk_fs, so a
  // --dry-run still consults them. Inactive is zero overhead.
  // -g / --gitignore: on forces .gitignore, auto enables it only when a search root
  // is inside a git repo (probe once, before the walk). -u / --no-ignore still wins:
  // ResolveGitignoreMode returns kOff then, so the repo probe and the global-excludes
  // read below are skipped too.
  const GitignoreMode gitignore_mode = ResolveGitignoreMode(command.globals, style);
  const bool gitignore_on = gitignore_mode == GitignoreMode::kOn
                            || (gitignore_mode == GitignoreMode::kAuto && AnyRootInRepo(walk_fs, command.roots));
  // --skip-vcs[=LIST] / --no-skip-vcs: the VCS metadata dir names to prune. Resolved once (needs
  // gitignore_on for the -g -> .git default) and validated here, so a bad token is a usage error
  // (exit 2) refused before the walk.
  const absl::StatusOr<absl::flat_hash_set<std::string>> skip_vcs = ResolveSkipVcs(command.globals, gitignore_on);
  if (!skip_vcs.ok()) {
    on_error("--skip-vcs", skip_vcs.status());
    return 2;
  }
  const absl::flat_hash_set<std::string>& skip_vcs_names = *skip_vcs;
  // git's global excludes (core.excludesFile, else ~/.config/git/ignore): the lowest
  // ignore layer, resolved once when gitignore is on. Read through walk_fs so --dry-run
  // still consults it; empty (a no-op) otherwise.
  ignore::PatternList global_excludes;
  if (gitignore_on) {
    const char* const home = std::getenv("HOME");
    const char* const xdg = std::getenv("XDG_CONFIG_HOME");
    const repo::GitConfigEnv env{.home = home == nullptr ? "" : home, .xdg_config_home = xdg == nullptr ? "" : xdg};
    if (const std::optional<std::string> path = repo::GlobalExcludesPath(walk_fs, env)) {
      if (const absl::StatusOr<std::string> content = walk_fs.ReadContent(*path); content.ok()) {
        global_excludes = ignore::PatternList::Parse(*content);
      }
    }
  }
  IgnoreFileCache ignore_files(
      walk_fs, ResolveIgnoreFileNames(command.globals, gitignore_on, style), gitignore_on, std::move(global_excludes));
  // --ignore-file=PATH: explicit ignore files, each rooted at its own directory. Independent of
  // -g / --no-ignore (the user named these), consulted below CLI --exclude but above the auto
  // .gitignore stack (an explicitly named file outranks auto-discovery, below a direct glob).
  const RootedIgnoreFiles rooted_ignore_files = RootedIgnoreFiles::FromGlobals(walk_fs, command.globals);

  // -ok confirmation: prompt to stderr, read a line from stdin, affirmative on y/Y (like find).
  const auto confirm = [](std::string_view prompt) -> bool {
    std::cerr << prompt;
    std::string line;
    if (!std::getline(std::cin, line)) {
      return false;  // EOF or closed stdin -> decline
    }
    return !line.empty() && (line[0] == 'y' || line[0] == 'Y');
  };

  // File-output actions (-fprint/-fprint0/-fprintf/-fls) append to a named file,
  // opened once (truncating) on first write and held open for the whole walk. The
  // visitor is single-threaded, so the sink map needs no synchronisation. Streams
  // close (flushing) when `file_sinks` goes out of scope after the walk.
  std::map<std::string, std::ofstream> file_sinks;
  const auto emit_file = [&file_sinks](std::string_view file, std::string_view record) {
    const std::string name(file);
    auto it = file_sinks.find(name);
    if (it == file_sinks.end()) {
      it = file_sinks.emplace(name, std::ofstream(name, std::ios::binary | std::ios::trunc)).first;
    }
    it->second.write(record.data(), static_cast<std::streamsize>(record.size()));
  };

  // -ls aligned output: each -ls row's cells feed a ColumnBuffer (per --buffer), whose
  // ready output is emitted as it forms and whose remainder is flushed after the walk.
  // Built once here so the computed column widths span the whole run. The visitor is
  // single-threaded, so the buffer needs no synchronisation.
  std::vector<format::Align> ls_aligns;
  std::vector<std::size_t> ls_mins;
  for (const LsColumn& column : LsColumns()) {
    ls_aligns.push_back(column.align);
    ls_mins.push_back(column.min_width);
  }
  const BufferBound ls_bound = ResolveBufferBound(command.globals, 100);  // -ls default: auto=100
  format::ColumnBuffer ls_buffer(std::move(ls_aligns), std::move(ls_mins), ls_bound.window, ls_bound.byte_budget);
  const auto emit_ls_row = [&ls_buffer, &emit](std::vector<std::string> cells) {
    if (const std::string ready = ls_buffer.Add(std::move(cells)); !ready.empty()) {
      emit(ready);
    }
  };

  // `-exec/-execdir ... +`: each batch node's matched items accrue here during the
  // walk and run at the end. Outer key the Expr node; inner key the directory ("" =
  // -exec's single global batch, the entry's dir = -execdir's per-dir batches). The
  // visitor is single-threaded, so no synchronisation is needed.
  std::map<const parser::Expr*, std::map<std::string, std::vector<std::string>>> exec_batches;

  // -j>1: `-exec/-execdir ... ;` children run concurrently on this bounded runner,
  // capped at the same worker count as the walk (docs/design-parallel.md's single
  // knob). It is wired into the context only when workers > 1; at -j1 (and the
  // in-process default) the actions stay synchronous and this stays idle.
  exec::ParallelExec parallel_exec(options.workers);

  // Impossible-task policy (design.md "Exit-code model"): a predicate that cannot be
  // evaluated correctly on an entry's filesystem (e.g. -Btime where birth time is
  // unrecorded) signals via control.unsupported. By default that is a hard error
  // (exit 2); --skip-unsupported downgrades it to a warning and skips the entry.
  // Reported once per run (a representative path) rather than once per entry, so a
  // whole btime-less tree does not flood stderr.
  const bool skip_unsupported = HasGlobal(command.globals, "--skip-unsupported");
  bool unsupported_reported = false;

  // FS-native name matching (design.md "macOS / cross-platform correctness"; #45):
  // the xff style matches -name/-path the way the entry's own volume resolves
  // names, so a lookup the OS would satisfy case-insensitively (APFS/HFS+ default,
  // NTFS) also matches here. --exact opts out (verbatim byte-exact), and the find
  // style is always byte-exact (drop-in faithful). When active, each entry's volume
  // is probed once (cached by device id; the visitor is single-threaded, so the
  // cache needs no synchronisation), and a case-folding volume sets fold_name_case.
  const bool fs_native_case = style == registry::Style::kXff && !HasGlobal(command.globals, "--exact");
  absl::flat_hash_map<std::uint64_t, bool> case_sensitive_by_dev;
  // --hidden / --no-hidden (style-scoped default): whether to drop hidden dotfiles.
  const bool skip_hidden = ResolveSkipHidden(command.globals, style);

  // The header row (the column names, or the single default `path`) prints unless --no-header,
  // and only for the implicit path listing (not a --summary or explicit-action stream).
  const bool table_header_shown = implicit_print && !any_reduction && !HasGlobal(command.globals, "--no-header");

  // Buffered tabular output (--format=aligned/markdown) streams each match's cells through a
  // TableStream: it buffers up to the --buffer window (default all = full alignment) to size the
  // columns, then emits the header + rule + rows and streams the rest at the locked widths. The
  // visitor is single-threaded, so no lock.
  std::optional<render::TableStream> table_stream;
  if (buffered) {
    std::vector<std::string> table_columns = columns.empty() ? std::vector<std::string>{"path"} : columns;
    const BufferBound bound = ResolveBufferBound(command.globals, render::TableStream::kAll);  // tables: all
    table_stream.emplace(format, std::move(table_columns), table_header_shown, bound.window, bound.byte_budget);
  }

  // --format=tree splices each matched path into a shared-prefix structure (its ancestors become
  // branch nodes), rendered depth-first after the walk. --unicode picks box-drawing vs ASCII.
  std::optional<render::Tree> tree;
  if (is_tree) {
    tree.emplace(ResolveUnicode(command.globals));
  }

  // Streaming tabular (csv/tsv) emits its one-time header row here; the buffered formats emit
  // theirs inside TableStream, so Header() / EncodeTabularRow() return "" and this is a no-op.
  if (table_header_shown) {
    const std::string header = columns.empty() ? render::Renderer(format, path_encoding).Header()
                                               : render::EncodeTabularRow(format, columns);  // the column names
    if (!header.empty()) {
      emit(header);
    }
  }

  const absl::Status status = Walk(
      walk_fs, command.roots, options,
      [&](const Visit& visit) {
        // Hidden filter: unless hidden files are included, drop a dotfile (basename
        // starting with '.') before any evaluation or output. A hidden directory is
        // pruned (its whole subtree skipped); a hidden file is skipped. Depth 0 is an
        // explicitly named search root, always entered -- so `xff .git` still descends.
        if (skip_hidden && visit.depth > 0 && !visit.name.empty() && visit.name.front() == '.') {
          return visit.metadata.type == vfs::FileType::kDirectory ? WalkAction::kPrune : WalkAction::kContinue;
        }
        // VCS metadata filter: prune version-control plumbing directories (--skip-vcs; `-g` implies
        // `.git`), like ripgrep / fd. Git never lists `.git` in a .gitignore -- it excludes its own
        // plumbing implicitly -- so the ignore rules alone never drop it. At any depth and
        // deliberately independent of the hidden filter, so hidden files the user keeps (`.bazelrc`,
        // `.gitignore`, ...) still show; only VCS plumbing is dropped. Depth 0 is an explicitly named
        // root, always entered, so `xff .git` still descends. `skip_vcs_names` holds the metadata
        // names (`.git`, `.hg`, ...); it is empty (this filter off) unless --skip-vcs or -g is active.
        if (!skip_vcs_names.empty() && visit.depth > 0 && skip_vcs_names.contains(visit.name)) {
          return visit.metadata.type == vfs::FileType::kDirectory ? WalkAction::kPrune : WalkAction::kContinue;
        }
        // Ignore filter: drop an ignored entry before any evaluation or output. A
        // matched directory is pruned (its subtree is never walked, so this is also
        // the fast path); a matched file is simply skipped. The search root itself
        // (empty relative path) is never filtered -- the user named it. CLI
        // --exclude/--include has highest precedence, then explicit --ignore-file sources
        // (rooted at the file's own dir), then the auto per-directory ignore-file stack
        // (--ignore-files / -g); each later layer decides only where the earlier ones are silent.
        if (!ignore_patterns.empty() || rooted_ignore_files.active() || ignore_files.active()) {
          const bool is_dir = visit.metadata.type == vfs::FileType::kDirectory;
          const std::string_view rel = RelativeTo(visit.path, visit.root);
          if (!rel.empty()) {
            ignore::Decision decision = ignore_patterns.Match(rel, is_dir);
            if (decision == ignore::Decision::kDefault && rooted_ignore_files.active()) {
              decision = rooted_ignore_files.Decide(AbsoluteDir(visit.path), is_dir);
            }
            if (decision == ignore::Decision::kDefault && ignore_files.active()) {
              decision = ignore_files.Decide(visit.path, visit.root, is_dir);
            }
            if (decision == ignore::Decision::kIgnore) {
              return is_dir ? WalkAction::kPrune : WalkAction::kContinue;
            }
          }
        }
        Control control;
        std::vector<std::string> captures;           // -regex groups for this entry; consumed by gated -exec {0}..{N}
        std::map<std::string, std::string> outputs;  // -capture results for this entry; read by {capture.NAME}
        // FS-native name matching: fold -name/-path case on a case-folding volume
        // (xff style, no --exact). Probe each device once, defaulting to
        // case-sensitive (byte-exact) on the miss and on any probe error.
        bool fold_name_case = false;
        if (fs_native_case) {
          auto [it, inserted] = case_sensitive_by_dev.try_emplace(visit.metadata.dev, true);
          if (inserted) {
            it->second = walk_fs.IsCaseSensitive(visit.path).value_or(true);
          }
          fold_name_case = !it->second;
        }
        EvalContext eval_context{
            .visit = visit,
            .emit = emit,
            .emit_file = emit_file,
            .emit_ls_row = emit_ls_row,
            .ls_size_units = human,
            .fs = walk_fs,
            .now = now,
            .tz = tz,
            .time_format = time_format,
            .block_size = block_size,
            .fold_name_case = fold_name_case,
            .grep_count = grep_count,
            .grep_before = grep_before,
            .grep_after = grep_after,
            .diff_algorithm = diff_algorithm,
            .diff_ignore = diff_ignore,
            .diff_ignore_matching = diff_ignore_matching,
            .diff_format = diff_format,
            .diff_context = diff_context,
            .hash_algorithm = hash_algorithm,
            .hash_encoding = hash_encoding,
            .control = control,
            .exec_fields = exec_fields,
            .captures = exec_fields ? &captures : nullptr,
            .defines = &defines,
            .outputs = &outputs,
            .confirm = confirm,
            .exec_batches = &exec_batches,
            .parallel_exec = options.workers > 1 ? &parallel_exec : nullptr};
        const bool matched = expression == nullptr || Evaluate(*expression, eval_context);
        if (matched && any_match != nullptr) {
          *any_match = true;  // grep-style "found anything" -- the expression's truth, not output
        }
        if (matched && any_reduction) {
          // --summary / --histogram reduce matches instead of printing them; explicit
          // actions (-print/-exec) still ran via Evaluate.
          if (summary_mode == SummaryMode::kTemplate) {
            // A field-template key. Build the render context once; an m// extraction contributes a
            // value stream (one count per extracted line, size not attributed -- a per-line key would
            // double-count the file's size), a plain template one key per matched entry (size
            // meaningful). {target} in a summary key is unsupported here (link_target left empty).
            const std::string link;
            const fields::RenderContext key_ctx{
                .path = visit.path,
                .root = visit.root,
                .link_target = link,
                .metadata = visit.metadata,
                .depth = visit.depth,
                .tz = tz,
                .time_format = time_format,
                .hash_algorithm = hash_algorithm,
                .hash_encoding = hash_encoding,
                .defines = &defines,
                .outputs = &outputs};
            if (const std::optional<std::vector<std::string>> stream = summary_template->AsExtraction(key_ctx);
                stream.has_value()) {
              for (const std::string& key : *stream) {
                summary[key].first += 1;
              }
            } else {
              std::pair<std::uint64_t, std::uint64_t>& agg = summary[summary_template->Render(key_ctx)];
              agg.first += 1;
              agg.second += visit.metadata.size;
            }
          } else if (summary_mode != SummaryMode::kOff) {
            std::pair<std::uint64_t, std::uint64_t>& agg = summary[SummaryKey(summary_mode, visit)];
            agg.first += 1;
            agg.second += visit.metadata.size;
          }
          for (std::size_t i = 0; i < histograms.size(); ++i) {
            const HistogramSpec& spec = histograms[i];
            const std::optional<std::pair<std::string, std::string>> bucket = HistBucketKey(spec, visit);
            if (!bucket.has_value()) {
              continue;  // the bucket field is unavailable here (e.g. a lines bucket for a binary file)
            }
            std::optional<std::uint64_t> value = 1;  // kCount: each match contributes one
            if (spec.agg != HistAgg::kCount) {
              if (spec.metric == HistMetric::kSize) {
                value = visit.metadata.size;
              } else {  // kLines: content-derived, absent for a non-regular or binary file
                value =
                    visit.metadata.type == vfs::FileType::kRegular ? content::FileLineCount(visit.path) : std::nullopt;
              }
            }
            if (!value.has_value()) {
              continue;  // no value for this metric on this entry (e.g. a binary file for lines)
            }
            HistCell& cell = histogram_cells[i][bucket->first];
            if (cell.count == 0) {
              cell.label = bucket->second;  // display text, set once when the bucket is first seen
            }
            cell.min = cell.count == 0 ? *value : std::min(cell.min, *value);
            cell.max = cell.count == 0 ? *value : std::max(cell.max, *value);
            cell.sum += *value;
            cell.count += 1;
          }
        } else if (matched && implicit_print) {
          if (is_tree) {
            tree->Add(visit.path);  // splice into the tree; rendered whole after the walk
          } else if (buffered && column_templates.empty() && !compiled_tmpl.has_value()) {
            // Buffered tabular (aligned/markdown) with the default single `path` column: no
            // field vocabulary needed, so stream the raw path directly.
            emit(table_stream->Add({std::string(visit.path)}));
          } else if (!column_templates.empty() || compiled_tmpl.has_value()) {
            // --columns / --template render through the field vocabulary; build the context
            // once. {target} is a symlink's target (find %l), symlink-gated so a non-symlink
            // costs no syscall; `link` owns the text for the Render.
            std::string link;
            if (visit.metadata.type == vfs::FileType::kSymlink) {
              if (const absl::StatusOr<std::string> target = walk_fs.ReadLink(visit.path); target.ok()) {
                link = *target;
              }
            }
            const fields::RenderContext ctx{
                .path = visit.path,
                .root = visit.root,
                .link_target = link,
                .metadata = visit.metadata,
                .depth = visit.depth,
                .tz = tz,
                .time_format = time_format,
                .hash_algorithm = hash_algorithm,
                .hash_encoding = hash_encoding,
                .defines = &defines,
                .outputs = &outputs};
            if (!column_templates.empty()) {  // --columns: a tabular row of field values
              std::vector<std::string> cells;
              cells.reserve(column_templates.size());
              for (const fields::Template& column : column_templates) {
                cells.push_back(column.Render(ctx));
              }
              if (buffered) {  // aligned/markdown: stream through the buffer (windowed by --buffer)
                emit(table_stream->Add(cells));
              } else {
                emit(render::EncodeTabularRow(format, cells));
              }
            } else {  // --template overrides --format
              emit(compiled_tmpl->Render(ctx) + "\n");
            }
          } else {
            const std::string_view color =
                colorize ? color::CodeForType(visit.metadata.type, visit.metadata.mode) : std::string_view();
            emit(render::Renderer(format, path_encoding).Record(visit.path, color));
          }
        }
        if (!control.unsupported.empty() && !unsupported_reported) {
          unsupported_reported = true;  // once per run, not per entry
          if (skip_unsupported) {
            on_error(visit.path, absl::FailedPreconditionError(absl::StrCat(control.unsupported, " (skipped)")));
          } else {
            on_error(
                visit.path, absl::FailedPreconditionError(
                                absl::StrCat(control.unsupported, "; use --skip-unsupported to skip such entries")));
            ++errors;  // impossible task -> hard error (exit 2)
          }
        }
        if (control.quit) {
          return WalkAction::kStop;
        }
        if (control.prune) {
          return WalkAction::kPrune;
        }
        return WalkAction::kContinue;
      },
      [&](std::string_view path, absl::Status error_status) {
        ++errors;
        on_error(path, error_status);
      });
  if (!status.ok()) {
    ++errors;  // Fatal traversal error (none today; per-path errors handled above).
  }

  // Flush any -ls rows still buffered for alignment (a run shorter than the --buffer
  // window, or --buffer=all). No-op when -ls was not used or nothing remains.
  if (const std::string ls_tail = ls_buffer.Flush(); !ls_tail.empty()) {
    emit(ls_tail);
  }

  // Flush the buffered tabular formats (--format=aligned/markdown): emit whatever the
  // TableStream still holds (a run shorter than the --buffer window, or --buffer=all), plus a
  // header-only table when nothing matched. A no-op once the window already streamed everything.
  if (table_stream.has_value()) {
    if (const std::string table = table_stream->Flush(); !table.empty()) {
      emit(table);
    }
  }

  // Render the tree (--format=tree) now that every matched path has been spliced in.
  if (tree.has_value()) {
    if (const std::string rendered = tree->Render(); !rendered.empty()) {
      emit(rendered);
    }
  }

  // -j>1: reap every concurrent `-exec/-execdir ... ;` child still running. find's
  // `;` form is a predicate -- a nonzero exit makes only the action false, it does
  // NOT affect find's exit status (verified against BSD/GNU find) -- so the drained
  // failure count is intentionally discarded here, keeping -jN identical to the
  // synchronous -j1 path. The `+` batch form is the one that does count failures,
  // and it runs through exec_batches just below. A no-op when nothing was launched.
  parallel_exec.Drain();

  // `-exec/-execdir ... +`: now that the walk is done, run each batch node's
  // accumulated items in ARG_MAX chunks -- -exec once over the global ("") bucket,
  // -execdir once per directory bucket (cwd = that dir). A nonzero exit is a
  // per-command error, as for `;`.
  for (const auto& [node, by_dir] : exec_batches) {
    const bool execdir = node->descriptor->name == "-execdir";
    for (const auto& [dir, items] : by_dir) {
      const bool ok = execdir ? exec::ExecuteBatchInDir(node->args, items, dir) : exec::ExecuteBatch(node->args, items);
      if (!ok) {
        ++errors;
        on_error(node->descriptor->name, absl::UnknownError("batched command exited non-zero"));
      }
    }
  }

  // --summary: emit the accumulated table -- one row per group (sorted, since
  // `summary` is an ordered map) plus a `total` row (the overall mode already has a
  // single group keyed "total"). Default is a right-aligned human table (grouped
  // digits); --format=jsonl emits one machine object per row instead.
  if (summary_mode != SummaryMode::kOff) {
    struct Row {
      std::string key;
      std::uint64_t count = 0;
      std::uint64_t size = 0;
    };

    std::vector<Row> rows;
    std::uint64_t total_count = 0;
    std::uint64_t total_size = 0;
    for (const auto& [key, agg] : summary) {
      rows.push_back(Row{.key = key, .count = agg.first, .size = agg.second});
      total_count += agg.first;
      total_size += agg.second;
    }
    // --top=N: keep the N largest groups by size (count, then key, break ties), in
    // that order; the total row below still reflects every matched group. Absent =>
    // all groups in the map's alphabetical order.
    if (const std::optional<std::size_t> top = ResolveTop(command.globals);
        top.has_value() && summary_mode != SummaryMode::kOverall) {
      absl::c_sort(rows, [](const Row& lhs, const Row& rhs) {
        if (lhs.size != rhs.size) {
          return lhs.size > rhs.size;
        }
        if (lhs.count != rhs.count) {
          return lhs.count > rhs.count;
        }
        return lhs.key < rhs.key;
      });
      if (rows.size() > *top) {
        rows.resize(*top);
      }
    }
    if (summary_mode != SummaryMode::kOverall) {
      rows.push_back(Row{.key = "total", .count = total_count, .size = total_size});
    }
    if (format == render::Format::kJsonl) {
      for (const Row& row : rows) {
        emit(absl::StrCat("{\"group\":", JsonQuote(row.key), ",\"count\":", row.count, ",\"bytes\":", row.size, "}\n"));
      }
    } else {
      // Label left, grouped count right. --human renders the size as two aligned columns
      // -- a right-aligned number (fixed fraction area, so decimal points line up and are
      // blanked for exact bytes) and a left-aligned unit suffix that starts at one column,
      // e.g. "12.34 MiB" over "512    B". Without --human the size stays raw grouped bytes,
      // right-aligned. The Table carries the per-column max-width context.
      if (human.has_value()) {
        const unsigned precision = ResolveSummaryPrecision(command.globals);
        std::vector<format::SizeParts> sizes;
        sizes.reserve(rows.size());
        std::size_t number_width = 0;
        for (const Row& row : rows) {
          format::SizeParts parts = format::SizeColumns(row.size, *human, precision);
          number_width = std::max(number_width, parts.number.size());
          sizes.push_back(std::move(parts));
        }
        format::Table table({format::Align::kLeft, format::Align::kRight, format::Align::kLeft});
        for (std::size_t i = 0; i < rows.size(); ++i) {
          table.AddRow(
              {rows[i].key, format::Int(rows[i].count, ','),
               absl::StrCat(format::PadLeft(sizes[i].number, number_width), " ", sizes[i].suffix)});
        }
        emit(table.Render());
      } else {
        format::Table table({format::Align::kLeft, format::Align::kRight, format::Align::kRight});
        for (const Row& row : rows) {
          table.AddRow({row.key, format::Int(row.count, ','), format::Int(row.size, ',')});
        }
        emit(table.Render());
      }
    }
  }

  // --histogram: after any --summary table, emit each histogram's bars (or jsonl rows), in the
  // order the flags were given. Bars scale to the tallest bucket; --top keeps the N tallest.
  // Unicode block bars on a UTF-8 locale (--unicode), ASCII '#' otherwise.
  if (!histograms.empty()) {
    const bool unicode = ResolveUnicode(command.globals);
    const std::optional<std::size_t> top = ResolveTop(command.globals);
    const unsigned precision = ResolveSummaryPrecision(command.globals);
    const std::size_t bar_width = ResolveHistogramWidth(command.globals);

    struct Bar {
      std::string label;
      HistValue value;
    };

    for (std::size_t i = 0; i < histograms.size(); ++i) {
      const bool numeric = IsNumericBucket(histograms[i].bucket);
      std::vector<Bar> bars;
      double max_scale = 0;
      // The map iterates in key order, i.e. the ascending range order for a numeric bucket.
      for (const auto& [key, cell] : histogram_cells[i]) {
        HistValue value = HistMeasureValue(histograms[i].agg, cell, precision);
        max_scale = std::max(max_scale, value.scale);
        bars.push_back(Bar{.label = cell.label, .value = std::move(value)});
      }
      // A categorical bucket sorts by bar height (tallest first) and honors --top; a numeric-range
      // bucket keeps the ascending range order (a distribution) and shows every range.
      if (!numeric) {
        absl::c_sort(bars, [](const Bar& lhs, const Bar& rhs) {
          if (lhs.value.scale > rhs.value.scale) {
            return true;
          }
          if (lhs.value.scale < rhs.value.scale) {
            return false;
          }
          return lhs.label < rhs.label;
        });
        if (top.has_value() && bars.size() > *top) {
          bars.resize(*top);
        }
      }
      if (format == render::Format::kJsonl) {
        for (const Bar& bar : bars) {
          emit(
              absl::StrCat(
                  "{\"histogram\":", JsonQuote(histograms[i].label), ",\"bucket\":", JsonQuote(bar.label),
                  ",\"value\":", bar.value.json, "}\n"));
        }
        continue;
      }
      // Text bars: the label left-padded to the widest, the value right-aligned, then the bar. The
      // bar is last so its Unicode width never disturbs the aligned columns.
      std::size_t label_width = 0;
      std::size_t value_width = 0;
      for (const Bar& bar : bars) {
        label_width = std::max(label_width, bar.label.size());
        value_width = std::max(value_width, bar.value.text.size());
      }
      for (const Bar& bar : bars) {
        const double fraction = max_scale == 0 ? 0.0 : bar.value.scale / max_scale;
        emit(
            absl::StrCat(
                bar.label, std::string(label_width - bar.label.size(), ' '), "  ",
                format::PadLeft(bar.value.text, value_width), "  ", HistogramBar(fraction, bar_width, unicode), "\n"));
      }
    }
  }

  return errors;
}

namespace {

// Display strings for the flavor feature-map, one per resolver's value type.
std::string GitignoreName(GitignoreMode mode) {
  switch (mode) {
    case GitignoreMode::kAuto: return "auto";
    case GitignoreMode::kOff: return "off";
    case GitignoreMode::kOn: return "on";
  }
  return "off";
}

std::string HiddenName(bool skip) {
  return skip ? "skip" : "show";
}

std::string HumanName(std::optional<format::SizeUnits> units) {
  if (!units.has_value()) {
    return "bytes";
  }
  return *units == format::SizeUnits::kSi ? "si" : "iec";
}

std::string SortName(SortOrder order) {
  switch (order) {
    case SortOrder::kNone: return "none";
    case SortOrder::kDir: return "per-dir";
    case SortOrder::kSubtree: return "subtree";
    case SortOrder::kTree: return "tree";
  }
  return "none";
}

std::string CaseName(parser::CaseMode mode) {
  switch (mode) {
    case parser::CaseMode::kInsensitive: return "insensitive";
    case parser::CaseMode::kSensitive: return "sensitive";
    case parser::CaseMode::kSmart: return "smart";
  }
  return "sensitive";
}

}  // namespace

std::vector<FlavorFacet> FlavorFacets() {
  return {
      {.behavior = "ignore files (.gitignore/.ignore)",
       .flag = "-g / --gitignore, --no-ignore",
       .value = [](const std::vector<std::string>& g,
                   registry::Style s) { return GitignoreName(ResolveGitignoreMode(g, s)); }},
      {.behavior = "hidden dotfiles",
       .flag = "--hidden / --no-hidden",
       .value = [](const std::vector<std::string>& g,
                   registry::Style s) { return HiddenName(ResolveSkipHidden(g, s)); }},
      {.behavior = "sizes",
       .flag = "--human",
       .value = [](const std::vector<std::string>& g, registry::Style s) { return HumanName(ResolveHuman(g, s)); }},
      {.behavior = "traversal order",
       .flag = "--sort",
       .value = [](const std::vector<std::string>& g, registry::Style s) { return SortName(ResolveSort(g, s)); }},
      {.behavior = "letter case",
       .flag = "--case, -i, -s[+|-]",
       .value = [](const std::vector<std::string>& g,
                   registry::Style s) { return CaseName(parser::ResolveCaseMode(g, s)); }},
  };
}

}  // namespace xff::engine
