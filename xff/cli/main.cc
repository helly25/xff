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

#include <unistd.h>

#include <array>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "xff/cli/globals.h"
#include "xff/cli/help.h"
#include "xff/cli/help_backend.h"
#include "xff/cli/help_build.h"
#include "xff/cli/help_width.h"
#include "xff/cli/manpage.h"
#include "xff/cli/markdown.h"
#include "xff/cli/pager.h"
#include "xff/cli/plain_backend.h"
#include "xff/color/color.h"
#include "xff/config/config.h"
#include "xff/config/loader.h"
#include "xff/config/policy.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/run.h"
#include "xff/env/env.h"
#include "xff/format/format.h"
#include "xff/parser/parser.h"
#include "xff/regex/regex.h"
#include "xff/registry/descriptor.h"
#include "xff/vfs/local_fs.h"

namespace {

// Environment variable as an optional (nullopt when unset), for config discovery.
std::optional<std::string> EnvOpt(std::string_view name) {
  return xff::env::Get(name);
}

// Reads a whole file, or nullopt if it cannot be opened: the config FileReader.
std::optional<std::string> ReadFile(std::string_view path) {
  const std::ifstream in{std::string(path), std::ios::binary};
  if (!in) {
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

// The flavor feature-map: one row per style-scoped behavior, its controlling flag(s), each
// style's default, and (when `current` is set) the value resolved for this invocation. All
// values come from engine::FlavorFacets() -- each facet wraps its own resolver -- so the table
// cannot drift from an actual run. `--help=styles` shows the static comparison (no `current`);
// `--explain` adds the `current` column from the resolved style + globals.
//
// Two-tier: the facets that actually vary lead (the styles disagree, or -- in --explain -- a flag
// overrode the active style's default), then the facets identical in every style follow, so the
// signal is not buried among the many behaviors that never differ.
std::string RenderFlavorTable(const std::vector<std::string>& globals, std::optional<xff::registry::Style> current) {
  using xff::registry::Style;
  const std::size_t columns = current.has_value() ? 6 : 5;
  std::vector<std::string> header = {"behavior", "flag", "find", "xff", "rg"};
  if (current.has_value()) {
    header.emplace_back("current");
  }

  struct FacetRow {
    std::vector<std::string> cells;
    bool relevant = false;
  };

  std::vector<FacetRow> rows;
  for (const xff::engine::FlavorFacet& facet : xff::engine::FlavorFacets()) {
    const std::string find = facet.value({}, Style::kFind);
    const std::string xff = facet.value({}, Style::kXff);
    const std::string rg = facet.value({}, Style::kRg);
    std::vector<std::string> cells = {std::string(facet.behavior), std::string(facet.flag), find, xff, rg};
    bool relevant = !(find == xff && xff == rg);  // the styles disagree on this behavior
    if (current.has_value()) {
      const std::string resolved = facet.value(globals, *current);
      cells.push_back(resolved);
      if (resolved != facet.value({}, *current)) {
        relevant = true;  // a flag overrode the active style's default this run
      }
    }
    rows.push_back({.cells = std::move(cells), .relevant = relevant});
  }
  const auto section = [&](std::string_view title, bool want_relevant) -> std::string {
    xff::format::Table table(std::vector<xff::format::Align>(columns, xff::format::Align::kLeft));
    table.AddRow(header);
    std::size_t count = 0;
    for (const FacetRow& row : rows) {
      if (row.relevant == want_relevant) {
        table.AddRow(row.cells);
        ++count;
      }
    }
    return count == 0 ? std::string() : absl::StrCat(title, "\n", table.Render());
  };
  const std::string_view lead_title = current.has_value() ? "Relevant to this run:" : "Where the styles differ:";
  std::string out = section(lead_title, /*want_relevant=*/true);
  if (const std::string same = section("Same in every style:", /*want_relevant=*/false); !same.empty()) {
    absl::StrAppend(&out, out.empty() ? "" : "\n", same);
  }
  return out;
}

// The --help=extras topic: the optional build-time features and whether THIS binary links each.
// Availability is per-binary, so this is a runtime topic (not folded into the static --help=full /
// man / markdown reference). PCRE2 is the --regextype value extra (regex::Pcre2Available, from the
// self-registering @xff_pcre2 backend); the flag-gated extras come from the globals SOT (each
// distinct GlobalFlag.extra key, ExtraEnabled) so the list cannot drift from the flags.
std::string RenderExtras() {
  std::string out =
      "xff build extras. Optional features compiled in at build time; a lean build omits them, and a "
      "full build (--config=xff_full, or the installed `xff_full` binary) includes them all. Where an "
      "extra is not built in, its option stays listed but is a hard error if used.\n\n";
  const auto row = [&out](std::string_view name, bool built_in, std::string_view rebuild, std::string_view what) {
    absl::StrAppendFormat(
        &out, "  %-9s %s\n            %s\n", name,
        built_in ? "[built into this binary]" : absl::StrCat("[not built in; rebuild with ", rebuild, "]"), what);
  };
  row("pcre2", xff::regex::Pcre2Available(), "--//xff:xff_pcre",
      "--regextype=PCRE2: Perl-compatible regex (lookahead, backreferences, ...)");
  absl::flat_hash_set<std::string_view> seen;
  for (const xff::cli::GlobalFlag& flag : xff::cli::Globals()) {
    if (!flag.extra.empty() && seen.insert(flag.extra).second) {
      row(flag.extra, xff::cli::ExtraEnabled(flag.extra), absl::StrCat("--//xff:", flag.extra), flag.summary);
    }
  }
  return out;
}

// forward declaration (FullReference recurses); `context` carries the render meta
// (wrap width, ...) every model-rendered topic applies.
absl::StatusOr<std::string> RenderTopic(std::string_view topic, xff::cli::HelpRenderContext context);

// The full detailed reference (--help=full / long and --help-full / --help-long): every
// option and primary with explanations, then each sub-vocabulary topic marked in_full --
// so adding a topic auto-includes it here, no hand-maintained list.
std::string FullReference(xff::cli::HelpRenderContext context) {
  // The complete reference is the whole help model (the same Document --man / --markdown
  // render), in plain text and wrapped to the context width.
  xff::cli::PlainTextBackend backend(context);
  xff::cli::RenderDocument(xff::cli::BuildReference(), backend);
  return backend.Take();
}

// The single dispatch for `--help=TOPIC`: the CLI-rendered topics (needing the engine /
// datetime / flavor facets), else the registry-backed cli::RenderHelp. Shared by the
// --help= handler, the --help-* shortcuts, and FullReference (which never asks for the
// self-referential full/long, so there is no recursion).
absl::StatusOr<std::string> RenderTopic(std::string_view topic, xff::cli::HelpRenderContext context) {
  if (topic == "styles" || topic == "flavors") {
    return RenderFlavorTable({}, std::nullopt);
  }
  // The sub-vocabulary topics (fields / printf / time / size / grammars) and the index
  // topics (list / all / expressions) render from the model so they wrap + indent.
  {
    std::optional<xff::cli::Document> topic_doc = xff::cli::TopicReference(topic);
    if (!topic_doc.has_value()) {
      topic_doc = xff::cli::IndexReference(topic);
    }
    if (topic_doc.has_value()) {
      xff::cli::PlainTextBackend backend(context);
      xff::cli::RenderDocument(*topic_doc, backend);
      return backend.Take();
    }
  }
  if (topic == "extras") {
    return RenderExtras();
  }
  if (topic == "full" || topic == "long") {
    return FullReference(context);
  }
  // Otherwise a single primary / flag entry, rendered from the model so it wraps (and
  // later colors) via the context. Topic names take precedence over same-named flags
  // (e.g. `config` resolves to the topic above, not the `--config` flag) because the
  // TopicReference / IndexReference branches run first.
  if (std::optional<xff::cli::Document> entry = xff::cli::EntryReference(topic); entry.has_value()) {
    xff::cli::PlainTextBackend backend(context);
    xff::cli::RenderDocument(*entry, backend);
    return backend.Take();
  }
  return absl::NotFoundError("");  // unknown topic; the caller composes the user-facing message
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): cohesive dispatch
// resolve config, dispatch meta flags, build + run the expression) is one cohesive sequence.
// NOLINTNEXTLINE(readability-function-cognitive-complexity): cohesive dispatch
int RunMain(int argc, char** argv) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): intentional
  const std::vector<std::string> args(argv + 1, argv + argc);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): intentional
  const char* const program = argv[0];

  // Read the fixed set of environment variables xff consults into the env cache up front, in one
  // locked pass, so every later read is a pure cache hit. Dynamic {env.NAME} field references are
  // not here (they are user-supplied); env::Get caches those lazily on first use.
  static constexpr std::array<std::string_view, 10> kKnownEnv = {
      "COLUMNS",         "HOME",       "LANG",      "LC_ALL", "LC_CTYPE", "NO_COLOR", "PAGER",
      "XDG_CONFIG_HOME", "XFF_CONFIG", "XFF_PAGER",
  };
  xff::env::Prewarm(absl::MakeConstSpan(kKnownEnv));

  // The plain-help wrap width (--width), resolved once so every help / topic render
  // shares it. A bare --width means auto; --width=VALUE carries the value. Scanned
  // here (like the help flags) since --help short-circuits before the full parse.
  std::optional<std::string_view> width_flag;
  for (const std::string& arg : args) {
    if (arg == "--width") {
      width_flag = "auto";
    } else if (arg.starts_with("--width=")) {
      width_flag = std::string_view(arg).substr(std::string_view("--width=").size());
    }
  }
  const absl::StatusOr<std::size_t> help_width =
      xff::cli::ResolveHelpWidth(width_flag, xff::cli::DetectTerminalWidth());
  if (!help_width.ok()) {
    std::cerr << "xff: " << help_width.status().message() << "\n";
    return 2;
  }
  const bool stdout_is_tty = ::isatty(STDOUT_FILENO) != 0;
  // --color drives help color too (auto = a tty with NO_COLOR unset; always overrides).
  // Scanned from argv like --width, since --help short-circuits before the full parse.
  const bool help_color = xff::color::Enabled(xff::color::ResolveWhen(args), stdout_is_tty, xff::env::Has("NO_COLOR"));
  const xff::cli::HelpRenderContext help_context{.width = *help_width, .color = help_color};
  // --pager pages the long meta / doc output below (help / man / markdown) on a terminal;
  // resolved from argv like the two above, and applied only to those surfaces.
  const xff::cli::PagerWhen pager_when = xff::cli::ResolvePagerWhen(args);

  // Help and version, scanned anywhere in the arguments (find prints usage on a
  // bare --help wherever it lands). xff stays flag-only -- no `help` subcommand --
  // so the grammar is identical in find and xff flavors; only the vocabulary
  // differs. Accepted forms:
  //   --help / -h        usage page
  //   -help              GNU find compatibility (single-dash long option)
  //   --help=TOPIC       xff: registry-backed help for one primary/operator/action
  //   --help= / =list    xff: the whole-vocabulary index
  //   --version          version
  //   -version           GNU find compatibility
  for (const std::string& arg : args) {
    if (arg == "--help" || arg == "-help" || arg == "-h") {
      xff::cli::PlainTextBackend backend(help_context);
      xff::cli::RenderDocument(xff::cli::BuildUsage(), backend);
      xff::cli::EmitPaged(backend.Take(), pager_when, stdout_is_tty);
      return 0;
    }
    if (arg == "--help-all") {
      // hyphenated shortcut for --help=all (summaries)
      xff::cli::EmitPaged(RenderTopic("all", help_context).value_or(""), pager_when, stdout_is_tty);
      return 0;
    }
    if (arg == "--help-full" || arg == "--help-long") {
      // hyphenated shortcut for --help=full (explained)
      xff::cli::EmitPaged(RenderTopic("full", help_context).value_or(""), pager_when, stdout_is_tty);
      return 0;
    }
    if (arg.starts_with("--help=")) {
      const std::string_view topic = std::string_view(arg).substr(7);
      const absl::StatusOr<std::string> help = RenderTopic(topic, help_context);
      if (help.ok()) {
        xff::cli::EmitPaged(*help, pager_when, stdout_is_tty);
        return 0;
      }
      std::cerr << "xff: no help topic '" << topic << "'\n";  // RenderTopic's only failure is unknown-topic
      return 2;
    }
    if (arg == "--version" || arg == "-version") {
      std::cout << "xff 0.0.0\n";  // short and machine-scraped: never paged
      return 0;
    }
    if (arg == "--man") {
      // roff(1); on a tty the man kind formats it (mandoc) so it reads like `man xff`,
      // while a redirect stays raw roff for `mandoc` / `man -l -` / installing as xff.1.
      xff::cli::EmitPaged(xff::cli::ManPage(), pager_when, stdout_is_tty, xff::cli::PagerKind::kMan);
      return 0;
    }
    if (arg == "--markdown") {
      // GitHub-renderable vocabulary reference
      xff::cli::EmitPaged(xff::cli::MarkdownReference(), pager_when, stdout_is_tty);
      return 0;
    }
  }

  // xff is flag-only -- there is no `help` / `version` subcommand. A user reaching
  // for one out of git/cargo habit would otherwise have the word silently taken as a
  // path to search, so (in the xff flavor only; find must keep `find help` meaning
  // "search ./help") catch a leading operand that names one and point at the flag.
  if (xff::config::DefaultStyleForProgram(program) != "find") {
    for (const std::string& arg : args) {
      if (arg == "--") {
        break;  // explicit end-of-options: the next token is deliberately an operand
      }
      if (arg.starts_with("-") || arg.starts_with("+")) {
        continue;  // a leading global, not yet the first operand
      }
      if (arg == "help" || arg == "version") {
        const std::string_view flag_hint =
            arg == "help" ? " for usage, or '--help=NAME' for one primary (e.g. '--help=-regex')" : "";
        std::cerr << "xff: '" << arg << "' is not a subcommand (xff is flag-only). Use '--" << arg << "'" << flag_hint
                  << ". To search a path literally named '" << arg << "', use './" << arg << "'.\n";
        return 2;
      }
      break;  // the first operand is something else; carry on to normal parsing
    }
  }

  absl::StatusOr<xff::parser::Command> parsed = xff::parser::Parse(args);
  if (!parsed.ok()) {
    std::cerr << "xff: " << parsed.status().message() << "\n";
    return 2;
  }
  xff::parser::Command command = *std::move(parsed);

  // Reject an unknown leading global option (usually a typo) with a usage error,
  // instead of silently ignoring it. Meta flags (--help / --version / --man /
  // --markdown) are already handled above, so they never reach here.
  for (const std::string& global : command.globals) {
    if (!xff::cli::IsKnownGlobal(global)) {
      std::cerr << "xff: unknown option '" << global << "'\n"
                << "Try 'xff --help' for usage, or 'xff --help=NAME' for one option.\n";
      return 2;
    }
  }

  // A composable-extra flag (e.g. --archive) is always recognized, but if the extra it needs is not
  // compiled into this binary, using it is a hard immediate error naming what to rebuild with - never
  // a silent no-op. Derived from the flag's SOT `extra` key + the compile-time ExtraEnabled map.
  for (const std::string_view global : command.globals) {
    const std::string_view name = global.substr(0, global.find('='));
    const xff::cli::GlobalFlag* const flag = xff::cli::LookupGlobal(name);
    if (flag != nullptr && !flag->extra.empty() && !xff::cli::ExtraEnabled(flag->extra)) {
      std::cerr << "xff: " << flag->name << ": this build has no " << flag->extra
                << " support; rebuild with --//xff:" << flag->extra << "\n";
      return 2;
    }
  }

  // Load the layered config (system + user + explicit --xffrc) and resolve the
  // effective flags. --explain writes that effective configuration and exits.
  xff::config::DiscoveryOptions opts = xff::config::SelectorsFromGlobals(command.globals);
  // argv[0] dispatch: the program name picks the base style (invoked as `find` ->
  // strict find; as `xff` or any other alias -> modern xff) as the lowest-precedence
  // selector, so an explicit --config still overrides it (design-config.md "CLI
  // selectors"). Prepended before discovery so find:/xff: .xffrc lines gate on it too.
  opts.configs.insert(opts.configs.begin(), std::string(xff::config::DefaultStyleForProgram(program)));
  opts.xff_config = EnvOpt("XFF_CONFIG");
  opts.xdg_config_home = EnvOpt("XDG_CONFIG_HOME");
  opts.home = EnvOpt("HOME");
  // Config is system + user + explicit --xffrc only; there is no auto-discovered project layer
  // (Option B, 2026-07-06), so the search roots do not feed config discovery.
  const xff::config::ConfigInputs inputs = xff::config::Discover(opts, ReadFile);
  // --allow-exec arms the dangerous directives an --xffrc file may carry, but only when it comes
  // from a trusted tier (the CLI, or the user/system config) - never from an --xffrc file itself,
  // so a named config cannot authorize its own -exec/-delete. The gate uses this to keep unarmed
  // --xffrc dangerous lines inert.
  const bool xffrc_armed = xff::config::ArmedFromTrustedTier(inputs, command.globals, "--allow-exec");
  std::vector<xff::config::Drop> drops;
  const xff::config::ConfigInputs gated = xff::config::GateConfig(inputs, xffrc_armed, &drops);
  const std::vector<xff::config::ResolvedFlag> resolved = xff::config::ResolveConfig(gated);
  if (absl::c_contains(command.globals, "--explain")) {
    std::cout << xff::config::ExplainSources(inputs.sources, xff::config::ActiveStyle(inputs.configs));
    std::cout << xff::config::ExplainConfig(resolved, command.globals);
    for (const xff::config::Drop& drop : drops) {
      std::cout << "dropped\t" << xff::config::DropMessage(drop) << "\n";
    }
    std::cout << "\n# flavor defaults per style, and the value resolved for this run:\n";
    std::cout << RenderFlavorTable(command.globals, xff::config::ActiveStyle(inputs.configs));
    return 0;
  }
  // A disallowed config line is dropped, never fatal: warn (self-documenting) and
  // carry on with the survivors (design-config.md "Enforcement & self-documentation").
  for (const xff::config::Drop& drop : drops) {
    std::string_view why = " - denied by config policy";
    if (drop.reason == xff::config::DropReason::kPresetOverload) {
      why = " - a config file cannot change a preset; use a named config (--config=NAME)";
    } else if (drop.reason == xff::config::DropReason::kUnarmedXffrc) {
      why = " - inert unless armed with --allow-exec (from the CLI or user/system config)";
    }
    std::cerr << "xff: ignoring " << xff::config::DropMessage(drop) << why << "\n";
  }
  // Apply the config: prepend the resolved flags to the globals so they take
  // effect, the CLI globals (already present, kept last) winning on conflict.
  std::vector<std::string> config_flags;
  config_flags.reserve(resolved.size());
  for (const xff::config::ResolvedFlag& flag : resolved) {
    config_flags.push_back(flag.flag);
  }
  command.globals.insert(command.globals.begin(), config_flags.begin(), config_flags.end());

  // The strict find style (--config=find) accepts only find's own vocabulary;
  // reject xff extensions (e.g. -println) so a find-style run behaves like GNU
  // find (design-config.md "CLI selectors"). The default xff style accepts all.
  const xff::registry::Style style = xff::config::ActiveStyle(inputs.configs);
  if (const absl::Status status = xff::parser::EnforceStyle(command, style); !status.ok()) {
    std::cerr << "xff: " << status.message() << "\n";
    return 2;
  }

  // Apply the resolved case mode to the matchers (--case / -i / -s[+|-]; rg defaults
  // smart), in place before the walk: sets folding on the case-sensitive matchers and
  // recompiles their pre-compiled regex. A no-op under the sensitive default.
  xff::parser::ApplyCaseMode(command, xff::parser::ResolveCaseMode(command.globals, style));

  // Walk the roots and evaluate the expression, printing matches. Per-path errors
  // -> exit 2 (the xff exit-code model; design.md "Exit-code model"). Match-sensitive
  // exit is opt-in: --quiet suppresses output and exits by match, --exit-match keeps
  // output but exits by match; either makes "1 = no match" reachable. An error still
  // outranks match status (exit 2).
  const bool quiet = absl::c_contains(command.globals, "--quiet") || absl::c_contains(command.globals, "-q");
  const bool match_sensitive = quiet || absl::c_contains(command.globals, "--exit-match");
  const xff::vfs::LocalFs fs;
  bool matched = false;
  const int errors = xff::engine::RunFind(
      command, fs,
      [quiet](std::string_view record) {
        if (!quiet) {  // --quiet suppresses output; the match is still recorded via `matched`
          std::cout.write(record.data(), static_cast<std::streamsize>(record.size()));
        }
      },
      [](std::string_view path, absl::Status status) {
        std::cerr << "xff: " << path << ": " << status.message() << "\n";
      },
      style, &matched);  // mode-scoped traversal defaults (modern -> sorted + parallel; find -> unordered)
  if (errors != 0) {
    return 2;  // an error outranks match status
  }
  return match_sensitive && !matched ? 1 : 0;
}

// Entry point: run xff, then flush stdout explicitly before the process exits.
// xff writes results and the help / man / markdown pages to std::cout, which is
// fully buffered when stdout is a pipe. An abnormal exit after main -- notably
// LeakSanitizer's _exit under `--config=asan` on Linux, which runs before libc++
// would flush the stream -- otherwise truncates large output (a partial `--man` or
// `--help=list`). Flushing here, before returning into the C++ exit sequence,
// guarantees the bytes are written regardless of what the exit path does next.
}  // namespace

int main(int argc, char** argv) {
  const int exit_code = RunMain(argc, argv);
  std::cout.flush();
  return exit_code;
}
