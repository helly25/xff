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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "xff/cli/globals.h"
#include "xff/cli/help.h"
#include "xff/cli/manpage.h"
#include "xff/cli/markdown.h"
#include "xff/config/config.h"
#include "xff/config/loader.h"
#include "xff/config/policy.h"
#include "xff/datetime/datetime.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/run.h"
#include "xff/format/format.h"
#include "xff/parser/parser.h"
#include "xff/registry/descriptor.h"
#include "xff/vfs/local_fs.h"

namespace {

// `--help` / `-h` text. Lists the whole-run options (globals) that are actually
// wired; the find expression vocabulary (tests/operators/actions) follows the
// roots and is documented in find(1) and docs/.
constexpr std::string_view kHelpText =
    R"(xff -- eXtended File Find: a find(1)-compatible file finder with modern extensions.

Usage:
  xff [option...] [path...] [expression]
  find [option...] [path...] [expression]   # strict find compatibility, when invoked as `find`

No path searches the current directory; no action prints each match (an implicit -print).

Options (whole-run, before the paths):
)";

// The Expression: section of the usage page, printed after the generated Help: section.
// The Help: section (help / doc flags + the --help=TOPIC index) is generated from the
// cli::HelpFlags() and cli::HelpTopics() SOTs so no help-flag text is hand-maintained
// here; see cli::RenderHelpSection().
constexpr std::string_view kHelpTextExpression =
    R"(
Expression: tests, operators, and actions applied to each entry, by group. Use
`--help=expressions` for the full annotated list and `--help=NAME` for one entry
(e.g. `--help=-regex`):
  Name / path   -name  -iname  -path  -regex  -lname
  Type / size   -type  -size  -blocks  -empty  -sparse  -mime (media type by extension)
  Time          -mtime  -atime  -ctime  -Btime  -newerXY   (units + compound durations)
  Owner / perm  -user  -group  -uid  -gid  -perm  -readable / -writable / -executable
  Content       -content / -icontent (literal),  -rxc / -irxc (regex) filter;  -grep[=FMT] prints match lines
  Compare       -cmp TARGET  (true when byte-identical to TARGET, a field template; e.g. '{def.B}/{relpath}')
  Operators     -a   -o   !   ( )   ,      xff: -xor  -nand  -nor  -xnor
  Actions       -print  -print0  -printf  -println  -ls  -exec / -execdir CMD ;|+  -delete  -prune  -quit  -ok
)";

// Environment variable as an optional (nullopt when unset), for config discovery.
std::optional<std::string> EnvOpt(const char* name) {
  const char* const value = std::getenv(name);
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::string(value);
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

// The absolute directories whose .xffrc ancestor chains form the project cascade:
// each search root resolved to an absolute path (a regular file -> its parent
// directory), or the current directory when no roots were given. The loader walks
// each one's ancestors; a non-existent root still contributes its ancestor chain.
std::vector<std::string> ProjectDirs(const std::vector<std::string>& roots) {
  namespace fs = std::filesystem;
  std::vector<std::string> inputs = roots;
  if (inputs.empty()) {
    inputs.emplace_back(".");
  }
  std::vector<std::string> dirs;
  dirs.reserve(inputs.size());
  for (const std::string& root : inputs) {
    std::error_code ec;
    fs::path abs = fs::absolute(root, ec);
    if (ec) {
      continue;
    }
    abs = abs.lexically_normal();
    if (fs::is_regular_file(abs, ec)) {
      abs = abs.parent_path();
    }
    dirs.push_back(abs.string());
  }
  return dirs;
}

// The flavor feature-map: one row per style-scoped behavior, its controlling flag(s), each
// style's default, and (when `current` is set) the value resolved for this invocation. All
// values come from engine::FlavorFacets() -- each facet wraps its own resolver -- so the
// table cannot drift from an actual run. `--help=styles` shows the static comparison (no
// `current`); `--explain` adds the `current` column from the resolved style + globals.
std::string RenderFlavorTable(const std::vector<std::string>& globals, std::optional<xff::registry::Style> current) {
  using xff::registry::Style;
  std::vector<xff::format::Align> aligns(current.has_value() ? 7 : 6, xff::format::Align::kLeft);
  xff::format::Table table(std::move(aligns));
  std::vector<std::string> header = {"behavior", "flag", "find", "xff", "xfd", "rg"};
  if (current.has_value()) {
    header.emplace_back("current");
  }
  table.AddRow(std::move(header));
  for (const xff::engine::FlavorFacet& facet : xff::engine::FlavorFacets()) {
    std::vector<std::string> row = {std::string(facet.behavior),   std::string(facet.flag),
                                    facet.value({}, Style::kFind), facet.value({}, Style::kXff),
                                    facet.value({}, Style::kXfd),  facet.value({}, Style::kRg)};
    if (current.has_value()) {
      row.push_back(facet.value(globals, *current));
    }
    table.AddRow(std::move(row));
  }
  return table.Render();
}

// The -printf directive reference (--help=printf, and appended to --help=full), rendered
// from engine::PrintfDocs() through the shared cli::RenderDocRows layout.
std::string RenderPrintfDocs() {
  std::string out = "PRINTF DIRECTIVES (-printf / -fprintf / -println FORMAT):\n";
  absl::StrAppend(&out, xff::cli::RenderDocRows("  ", xff::engine::PrintfDocs()));
  return out;
}

// The time-format vocabulary (--help=time), rendered from datetime::FormatDocs().
std::string RenderTimeDocs() {
  std::string out = "TIME FORMATS (--time-format=FMT, --timezone, and time-field {:qualifiers}):\n";
  absl::StrAppend(&out, xff::cli::RenderDocRows("  ", xff::datetime::FormatDocs()));
  return out;
}

// The -size unit vocabulary (--help=size), rendered from engine::SizeUnitDocs().
std::string RenderSizeDocs() {
  std::string out = "SIZE UNITS (-size / -blocks [+|-]N[unit]):\n";
  absl::StrAppend(&out, xff::cli::RenderDocRows("  ", xff::engine::SizeUnitDocs()));
  return out;
}

absl::StatusOr<std::string> RenderTopic(std::string_view topic);  // forward declaration (FullReference recurses)

// The full detailed reference (--help=full / long and --help-full / --help-long): every
// option and primary with explanations, then each sub-vocabulary topic marked in_full --
// so adding a topic auto-includes it here, no hand-maintained list.
std::string FullReference() {
  std::string out(xff::cli::RenderHelp("full").value_or(""));
  for (const xff::cli::HelpTopic& topic : xff::cli::HelpTopics()) {
    if (topic.in_full) {
      absl::StrAppend(&out, "\n", RenderTopic(topic.name).value_or(""));
    }
  }
  return out;
}

// The single dispatch for `--help=TOPIC`: the CLI-rendered topics (needing the engine /
// datetime / flavor facets), else the registry-backed cli::RenderHelp. Shared by the
// --help= handler, the --help-* shortcuts, and FullReference (which never asks for the
// self-referential full/long, so there is no recursion).
absl::StatusOr<std::string> RenderTopic(std::string_view topic) {
  if (topic == "styles" || topic == "flavors") {
    return RenderFlavorTable({}, std::nullopt);
  }
  if (topic == "printf") {
    return RenderPrintfDocs();
  }
  if (topic == "time") {
    return RenderTimeDocs();
  }
  if (topic == "size") {
    return RenderSizeDocs();
  }
  if (topic == "full" || topic == "long") {
    return FullReference();
  }
  return xff::cli::RenderHelp(topic);
}

}  // namespace

int RunMain(int argc, char** argv) {
  const std::vector<std::string> args(argv + 1, argv + argc);

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
      std::cout << kHelpText << xff::cli::RenderOptions("  ") << xff::cli::RenderHelpSection() << kHelpTextExpression;
      return 0;
    }
    if (arg == "--help-all") {
      std::cout << RenderTopic("all").value_or("");  // hyphenated shortcut for --help=all (summaries)
      return 0;
    }
    if (arg == "--help-full" || arg == "--help-long") {
      std::cout << RenderTopic("full").value_or("");  // hyphenated shortcut for --help=full (explained)
      return 0;
    }
    if (arg.starts_with("--help=")) {
      const std::string_view topic = std::string_view(arg).substr(7);
      const absl::StatusOr<std::string> help = RenderTopic(topic);
      if (help.ok()) {
        std::cout << *help;
        return 0;
      }
      std::cerr << "xff: no help topic '" << topic << "'\n";  // RenderTopic's only failure is unknown-topic
      return 2;
    }
    if (arg == "--version" || arg == "-version") {
      std::cout << "xff 0.0.0\n";
      return 0;
    }
    if (arg == "--man") {
      std::cout << xff::cli::ManPage();  // roff(1); pipe to `man -l -` or install as xff.1
      return 0;
    }
    if (arg == "--markdown") {
      std::cout << xff::cli::MarkdownReference();  // GitHub-renderable vocabulary reference
      return 0;
    }
  }

  // xff is flag-only -- there is no `help` / `version` subcommand. A user reaching
  // for one out of git/cargo habit would otherwise have the word silently taken as a
  // path to search, so (in the xff flavor only; find must keep `find help` meaning
  // "search ./help") catch a leading operand that names one and point at the flag.
  if (xff::config::DefaultStyleForProgram(argv[0]) != "find") {
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

  // Load the layered config (system + user + explicit --xffrc) and resolve the
  // effective flags. --explain writes that effective configuration and exits.
  xff::config::DiscoveryOptions opts = xff::config::SelectorsFromGlobals(command.globals);
  // argv[0] dispatch: the program name picks the base style (invoked as `find` ->
  // strict find; as `xff` or any other alias -> modern xff) as the lowest-precedence
  // selector, so an explicit --config still overrides it (design-config.md "CLI
  // selectors"). Prepended before discovery so find:/xff: .xffrc lines gate on it too.
  opts.configs.insert(opts.configs.begin(), std::string(xff::config::DefaultStyleForProgram(argv[0])));
  opts.xff_config = EnvOpt("XFF_CONFIG");
  opts.xdg_config_home = EnvOpt("XDG_CONFIG_HOME");
  opts.home = EnvOpt("HOME");
  opts.roots = ProjectDirs(command.roots);  // absolute dirs for the project .xffrc cascade
  const xff::config::ConfigInputs inputs = xff::config::Discover(opts, ReadFile);
  std::vector<xff::config::Drop> drops;
  xff::config::ConfigInputs gated = xff::config::GateConfig(inputs, &drops);
  // --project-config: a per-directory (project) .xffrc lives in a tree the user may not control,
  // so it is not applied unless explicitly enabled. Unless =on, drop the project layer before
  // resolving; =warn (the default) prints one stderr note when a project .xffrc was found, =off
  // stays silent. Sensitive/destructive lines and style selectors are user/system-only regardless
  // (GateConfig/style resolution never take them from a project file); this gates the safe subset
  // too, since the file's mere presence in an untrusted tree should not silently change a run.
  const xff::config::ProjectConfigMode project_mode = xff::config::ResolveProjectConfigMode(command.globals);
  if (project_mode != xff::config::ProjectConfigMode::kOn) {
    const bool project_found = absl::c_any_of(inputs.sources, [](const xff::config::ConfigSource& source) {
      return source.layer == xff::config::Source::kProject && source.found;
    });
    gated.project.clear();
    if (project_mode == xff::config::ProjectConfigMode::kWarn && project_found) {
      std::cerr << "xff: a per-directory .xffrc was found but ignored; use --project-config=on to apply it, "
                   "or --project-config=off to silence this note\n";
    }
  }
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
    const std::string_view why = drop.reason == xff::config::DropReason::kPresetOverload
                                     ? " - a config file cannot change a preset; use a named config (--config=NAME)"
                                     : " - denied by config policy";
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

  // Apply the resolved case mode to the matchers (--case / -i / -s[+|-]; xfd/rg default
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
int main(int argc, char** argv) {
  const int exit_code = RunMain(argc, argv);
  std::cout.flush();
  return exit_code;
}
