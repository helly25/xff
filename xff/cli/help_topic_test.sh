#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com)
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Binary-level test of `--help=TOPIC` topic help (the flag-only mechanism) and the
# guiding error when a user reaches for a `help` subcommand out of habit. Anchors on
# stable substrings, not exact wording.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${helly25_bashtest}"

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

test::help_topic_flag_prints_entry() {
  local out rc
  out="$("$(_xff_bin)" --help=-regex 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_matches '\-regex' "${out}"
  expect_output_contains 'regular expression' "${out}" # the summary
  expect_output_contains 'test' "${out}"               # kind tag
  expect_output_contains 'whole' "${out}"              # the per-primary details (whole-path anchoring)
}

test::help_full_shows_per_primary_details() {
  # --help=full renders each primary's long description (registry Descriptor.details), not just the
  # one-line summaries; --help=expressions (summaries only) does not.
  local full expr
  full="$("$(_xff_bin)" --help=full 2>&1)"
  expr="$("$(_xff_bin)" --help=expressions 2>&1)"
  expect_output_contains 'batches as many paths' "${full}"                 # from -exec details
  expect_output_contains 'needs --allow-exec' "${full}"                    # -exec sensitivity note in the details
  expect_output_contains 'reaches back a full relative duration' "${full}" # from -mtime details
  expect_output_not_contains 'batches as many paths' "${expr}"
  expect_output_not_contains 'reaches back a full relative duration' "${expr}"
}

test::help_time_primary_shows_details() {
  # A per-primary topic (`--help=mtime`) resolves the -mtime descriptor and shows its long
  # description, including the xff-only compound-span form and the find-compat note.
  local out
  out="$("$(_xff_bin)" --help=mtime 2>&1)"
  expect_output_contains 'reaches back a full relative duration' "${out}"
  expect_output_contains 'rejected by --config=find' "${out}"
}

test::help_matching_primaries_show_details() {
  # -path documents that its glob crosses `/` (unlike the shell); -rxc that its content regex is
  # unanchored, distinguishing it from -regex's whole-path anchoring.
  local path rxc
  path="$("$(_xff_bin)" --help=path 2>&1)"
  expect_output_contains 'DO match' "${path}" # `*`/`?` cross `/`
  rxc="$("$(_xff_bin)" --help=rxc 2>&1)"
  expect_output_contains 'unanchored' "${rxc}"
}

test::help_attribute_primaries_show_details() {
  # -perm documents the exact / all-of / any-of prefix grammar; -xtype the follow-the-target rule.
  local perm xtype
  perm="$("$(_xff_bin)" --help=perm 2>&1)"
  expect_output_contains 'ALL the listed bits' "${perm}" # the -MODE all-of rule
  xtype="$("$(_xff_bin)" --help=xtype 2>&1)"
  expect_output_contains "link's TARGET" "${xtype}"
}

test::help_action_primaries_show_details() {
  # -print documents the implicit-default rule; -grep the path:lineno:text form; -fprint the
  # opened-once file handling that anchors the -f* family.
  local print grep fprint
  print="$("$(_xff_bin)" --help=print 2>&1)"
  expect_output_contains 'DEFAULT action' "${print}"
  grep="$("$(_xff_bin)" --help=grep 2>&1)"
  expect_output_contains 'line-output companion of -rxc' "${grep}"
  fprint="$("$(_xff_bin)" --help=fprint 2>&1)"
  expect_output_contains 'opened once' "${fprint}"
}

test::help_reference_time_primaries_show_details() {
  # -newer documents the -newerXY matrix convention; -newermt the time-string (t) form.
  local newer newermt
  newer="$("$(_xff_bin)" --help=newer 2>&1)"
  expect_output_contains 'where each of X and Y is a=access' "${newer}"
  newermt="$("$(_xff_bin)" --help=newermt 2>&1)"
  expect_output_contains 'a timestamp xff parses' "${newermt}"
}

test::help_traversal_owner_operator_primaries_show_details() {
  # -maxdepth documents the global-positional rule; -user the name/numeric resolution; -a the
  # operator precedence order.
  local maxdepth user op
  maxdepth="$("$(_xff_bin)" --help=maxdepth 2>&1)"
  expect_output_contains 'global positional option' "${maxdepth}"
  user="$("$(_xff_bin)" --help=user 2>&1)"
  expect_output_contains 'passwd database' "${user}"
  op="$("$(_xff_bin)" --help=-a 2>&1)"
  expect_output_contains 'tightest to loosest' "${op}"
}

test::help_topic_flag_resolves_without_dash() {
  expect_matches '\-regex' "$("$(_xff_bin)" --help=regex 2>&1)"
}

test::help_config_explains_tiers_and_style_selection() {
  # `--help=config` explains the layering, style selection (--config / argv[0]), and arming, and
  # pulls the config flags from the SOT. Distinct from `--help=--config` (just that flag).
  local config flag
  config="$("$(_xff_bin)" --help=config 2>&1)"
  expect_output_contains 'lowest to highest precedence' "${config}" # the tier ordering
  expect_output_contains 'argv[0]' "${config}"                      # style selection by invocation name
  expect_output_contains 'NON-ARMING' "${config}"                   # the --xffrc arming rule
  expect_matches '\-\-allow-exec' "${config}"                       # a config flag pulled from the SOT
  # --help=--config is the single flag, not the topic (no tier explanation).
  flag="$("$(_xff_bin)" --help=--config 2>&1)"
  expect_output_not_contains 'lowest to highest precedence' "${flag}"
}

test::help_cookbook_lists_worked_examples() {
  # `--help=cookbook` (aliases examples / recipes) is the task-oriented recipe list; each recipe
  # carries a runnable command. It also folds into --help=full.
  local cookbook full
  cookbook="$("$(_xff_bin)" --help=cookbook 2>&1)"
  expect_output_contains 'xff cookbook' "${cookbook}"
  expect_output_contains 'git blame' "${cookbook}"                           # the flagship -exec recipe
  expect_output_contains 'xff --summary=ext' "${cookbook}"                   # a runnable command line
  expect_output_contains 'Ten largest files' "${cookbook}"                   # a recipe task heading
  expect_output_contains 'git blame' "$("$(_xff_bin)" --help=examples 2>&1)" # alias resolves
  full="$("$(_xff_bin)" --help=full 2>&1)"
  expect_output_contains 'xff cookbook' "${full}" # in_full folds it into the full reference
}

test::help_list_shows_grouped_index() {
  local out
  out="$("$(_xff_bin)" --help=list 2>&1)"
  expect_output_contains 'Tests:' "${out}"
  expect_output_contains 'Actions:' "${out}"
  expect_output_contains 'Operators:' "${out}"
}

test::help_expressions_lists_the_annotated_vocabulary() {
  # `--help=expressions` is the grouped Tests/Actions/Operators list with summaries,
  # the full list the usage overview points at.
  local out
  out="$("$(_xff_bin)" --help=expressions 2>&1)"
  expect_output_contains 'Tests:' "${out}"
  expect_output_contains 'Actions:' "${out}"
  expect_matches '\-content' "${out}" # an expression primary is listed
}

test::help_fields_lists_the_placeholder_vocabulary() {
  # `--help=fields` prints the {field} vocabulary: named fields grouped by heading,
  # aliases folded in, plus the dynamic namespaces and qualifiers.
  local out rc
  out="$("$(_xff_bin)" --help=fields 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_contains 'Path & name:' "${out}"    # a group heading
  expect_output_contains '{relpath}' "${out}"       # a named field
  expect_matches '\{name\} \{file\}' "${out}"       # an alias folded onto its canonical
  expect_output_contains '{env.NAME}' "${out}"      # a dynamic namespace
  expect_output_contains '{name:s/RE/R/f}' "${out}" # the rewrite qualifier
  expect_output_contains 'stem' "${out}"            # a path-component keyword (read from the SOT)
  # `--help=format` is NOT this topic: it resolves to the --format output-format flag.
  out="$("$(_xff_bin)" --help=format 2>&1)"
  expect_output_contains 'output format' "${out}"
  expect_output_not_contains '{relpath}' "${out}"
}

test::help_stats_documents_the_reductions() {
  # `--help=stats` documents --summary and --histogram. Their flags (and the bucket/measure
  # grammar, carried in --histogram's details) are pulled from the SOT via the "stats" topic tag.
  local out rc
  out="$("$(_xff_bin)" --help=stats 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_matches '\-\-summary' "${out}"
  expect_matches '\-\-histogram' "${out}"
  expect_output_contains 'sum(lines)' "${out}"          # the aggregate grammar (from --histogram details)
  expect_output_contains 'needs an aggregator' "${out}" # the no-bare-metric rule
}

test::help_printf_lists_the_directive_vocabulary() {
  # `--help=printf` prints the % directive table (from engine::PrintfDocs) plus the
  # %{field} escape; --help=full folds the same table in so the full reference is exhaustive.
  local out rc
  out="$("$(_xff_bin)" --help=printf 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_contains 'PRINTF DIRECTIVES' "${out}"
  expect_output_contains '%p' "${out}"                # a find % directive
  expect_output_contains '%{NAME}' "${out}"           # the xff field escape
  expect_output_contains 'see --help=fields' "${out}" # qualifier cross-reference
  expect_output_contains 'PRINTF DIRECTIVES' "$("$(_xff_bin)" --help=full 2>&1)"
}

test::help_time_and_size_list_their_vocabularies() {
  # `--help=time` prints the time-format presets (from datetime::FormatDocs); `--help=size`
  # the -size units (from engine::SizeUnitDocs). Both fold into --help=full.
  local out
  out="$("$(_xff_bin)" --help=time 2>&1)"
  expect_output_contains 'TIME FORMATS' "${out}"
  expect_output_contains 'iso8601' "${out}"
  expect_output_contains 'epoch' "${out}"
  out="$("$(_xff_bin)" --help=size 2>&1)"
  expect_output_contains 'SIZE UNITS' "${out}"
  expect_output_contains 'kibibytes' "${out}"
  # --help=full is exhaustive: it folds in the field, printf, time, and size vocabularies.
  out="$("$(_xff_bin)" --help=full 2>&1)"
  expect_output_contains 'TIME FORMATS' "${out}"
  expect_output_contains 'SIZE UNITS' "${out}"
  expect_output_contains 'PRINTF DIRECTIVES' "${out}"
}

test::help_notice_and_license_reproduce_the_texts() {
  # For single-file binary releases the program must REPRODUCE its notices, not point at files.
  # `--help=notice` (alias notices) embeds the verbatim NOTICE (third-party manifest) + the build
  # extras this binary has; `--help=license` (alias licenses) embeds the verbatim LICENSE (Apache-2.0).
  local notice license
  notice="$("$(_xff_bin)" --help=notice 2>&1)"
  expect_output_contains 'none (lean build)' "${notice}" # the build-dependent extras line (lean here)
  expect_output_contains 'RE2' "${notice}"               # a core component (from the reproduced NOTICE)
  expect_output_contains 'BSD-3-Clause' "${notice}"      # a component's SPDX id in the manifest
  license="$("$(_xff_bin)" --help=license 2>&1)"
  expect_output_contains 'Apache License' "${license}" # the reproduced LICENSE text, in full
  expect_output_contains 'Version 2.0' "${license}"    # ditto (not just a pointer to a file)
  # The plural aliases resolve to the same topics.
  expect_output_contains 'RE2' "$("$(_xff_bin)" --help=notices 2>&1)"
  expect_output_contains 'Apache License' "$("$(_xff_bin)" --help=licenses 2>&1)"
}

test::help_unknown_topic_exits_two() {
  local out rc
  out="$("$(_xff_bin)" --help=-nonesuch 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains 'no help topic' "${out}"
}

test::bare_help_operand_is_guided_not_a_subcommand() {
  # A user typing `xff help` out of git habit gets a guiding error (not a silent
  # attempt to search a path named "help").
  local out rc
  out="$("$(_xff_bin)" help 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains 'not a subcommand' "${out}"
  expect_matches '\-\-help' "${out}"
}

test::bare_help_operand_passes_through_in_find_mode() {
  # Invoked as `find`, `help` must stay a path operand (find compatibility), so the
  # xff guiding error must NOT fire.
  local tmp out
  tmp="$(mktemp -d)"
  cp "$(_xff_bin)" "${tmp}/find"
  out="$("${tmp}/find" help 2>&1)" || true
  rm -rf "${tmp}"
  expect_output_not_contains 'not a subcommand' "${out}"
}

test_runner
