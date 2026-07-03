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
#include "xff/cli/help.h"
#include "xff/cli/manpage.h"
#include "xff/cli/markdown.h"
#include "xff/config/config.h"
#include "xff/config/loader.h"
#include "xff/config/policy.h"
#include "xff/engine/run.h"
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
  Config / mode:
    --config=NAME       select a style or config layer (find = strict find, xff = modern); repeatable
    --no-config         ignore discovered .xffrc files
    --xffrc=FILE        also load a specific config file
    --explain           print the resolved configuration and exit
  Traversal:
    -H / -L / -P        symlinks: follow on the roots / follow everywhere / never (default -P)
    -j N, --jobs=N|all  worker count for the walk and concurrent -exec (all = every core)
    --sort[=none|dir|subtree|tree]   sibling/traversal ordering (default depends on the mode)
  Matching:
    --block-size=SIZE   bytes per -size block for a bare `-size N` / `-size Nb` (default 512; e.g. 4k)
    --exact             match -name/-path byte-exact (xff otherwise folds case on a case-folding volume)
    --regextype=RE2|EXACT  how -grep reads its pattern: RE2 regex (default) or EXACT literal
  Ignore / filter:
    --exclude=GLOB      skip paths matching a gitignore-style glob (repeatable; a matched dir is pruned)
    --include=GLOB      re-include paths a --exclude would skip (repeatable; last match wins)
    -g[+|-], --gitignore[=on|off]   respect .gitignore: -g = auto (in a git repo), -g+/=on always, -g-/=off never
    --ignore-files      respect per-directory .ignore / .xffignore files (off by default)
    --no-ignore, -u     disable all ignore-file processing (-u: rg/fd short form)
  Output:
    --format=plain|nul|jsonl   record format (plain default; nul = -print0; jsonl = JSON lines)
    --path-encoding=raw|escape plain-output path bytes: raw (verbatim) or escape (C-escape controls)
    --template=TEMPLATE        render each match through a field template ({path}, {name}, ...)
    --implicit-print=yes|no    force the default -print on or off
    --summary[=overall|type|ext]   print an aligned count + size table instead of each match
                        (grouped digits; --format=jsonl emits one machine object per row)
    --count, -c         with -grep, print a per-file matching-line count (path:count), not the lines
    --top=N             with --summary, show only the N largest groups by size (total still counts all)
    --color[=auto|always|never]   colorize the plain listing by file type (auto=a tty; honors NO_COLOR)
    --human[=iec|si|off]   size units for -ls / --summary: iec (KiB/MiB), si (kB/MB), off (bytes)
                        (default: xff style -> human, find style -> bytes)
    --buffer[=auto|off|all|N]   -ls column alignment: buffer rows to size columns
                        (auto = first 100 then stream; off = min widths; all; or N)
  Exit by match (grep-style):
    --quiet, -q         suppress output; exit 0 if anything matched, else 1 (-q: grep-compatible)
    --exit-match        keep output; exit 0 if anything matched, else 1
  Safety:
    --safe              refuse destructive actions (-delete / -exec)
    --dry-run           preview -delete without removing anything
    --skip-unsupported  warn and skip a predicate a filesystem cannot evaluate (e.g. -Btime), not fail
  Fields & exec:
    --exec-fields       render -exec tokens through the field vocabulary ({name}, {path}, ...)
    --define=NAME=VALUE define a value referenced as {def.NAME}
    --capture-override  allow a -capture NAME to be bound more than once (last wins)
  Time:
    --time-format=FMT   default format for time fields (a preset name or a strftime pattern)
    --timezone=ZONE, --tz=ZONE   zone for interpreting/formatting times (local, utc, an IANA name, or +HH:MM)
  Other:
    -h, --help, -help   print this help and exit (-help for GNU find compatibility)
    --help=NAME         print help for one primary, operator, action, or global flag
                        (e.g. --help=-regex, --help=--sort); --help=list prints the index
    --version, -version print the version and exit
    --man               print the man page (roff; pipe to `man -l -`) and exit
    --markdown          print a Markdown reference of all options and primaries and exit

Expression: tests, operators, and actions applied to each entry, by group. Use
`--help=expressions` for the full annotated list and `--help=NAME` for one entry
(e.g. `--help=-regex`):
  Name / path   -name  -iname  -path  -regex  -lname
  Type / size   -type  -size  -blocks  -empty  -sparse  -mime (media type by extension)
  Time          -mtime  -atime  -ctime  -Btime  -newerXY   (units + compound durations)
  Owner / perm  -user  -group  -uid  -gid  -perm  -readable / -writable / -executable
  Content       -content / -icontent (literal),  -rxc / -irxc (regex) filter;  -grep[=FMT] prints match lines
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
      std::cout << kHelpText;
      return 0;
    }
    if (arg.starts_with("--help=")) {
      const std::string_view topic = std::string_view(arg).substr(7);
      const absl::StatusOr<std::string> help = xff::cli::RenderHelp(topic);
      if (help.ok()) {
        std::cout << *help;
        return 0;
      }
      std::cerr << "xff: no help topic '" << topic << "'\n";  // RenderHelp's only failure is unknown-topic
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
  const xff::config::ConfigInputs gated = xff::config::GateConfig(inputs, &drops);
  const std::vector<xff::config::ResolvedFlag> resolved = xff::config::ResolveConfig(gated);
  if (absl::c_contains(command.globals, "--explain")) {
    std::cout << xff::config::ExplainSources(inputs.sources, xff::config::ActiveStyle(inputs.configs));
    std::cout << xff::config::ExplainConfig(resolved, command.globals);
    for (const xff::config::Drop& drop : drops) {
      std::cout << "dropped\t" << xff::config::DropMessage(drop) << "\n";
    }
    return 0;
  }
  // A disallowed config line is dropped, never fatal: warn (self-documenting) and
  // carry on with the survivors (design-config.md "Enforcement & self-documentation").
  for (const xff::config::Drop& drop : drops) {
    std::cerr << "xff: ignoring " << xff::config::DropMessage(drop) << " - denied by config policy\n";
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
