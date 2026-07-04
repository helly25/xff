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

#include "xff/parser/parser.h"

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/parser/ast.h"
#include "xff/regex/regex.h"
#include "xff/registry/descriptor.h"

namespace xff::parser {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsNull;
using ::testing::NotNull;

struct ParserTest : ::testing::Test {};

TEST_F(ParserTest, GlobalsRootsExpression) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({"--color", ".", "-type", "f"}));
  EXPECT_THAT(cmd.globals, ElementsAre("--color"));
  EXPECT_THAT(cmd.roots, ElementsAre("."));
  ASSERT_THAT(cmd.expression, NotNull());
  EXPECT_THAT(cmd.expression->kind, Expr::Kind::kPredicate);
  ASSERT_THAT(cmd.expression->descriptor, NotNull());
  EXPECT_THAT(cmd.expression->descriptor->name, "-type");
  EXPECT_THAT(cmd.expression->args, ElementsAre("f"));
}

TEST_F(ParserTest, NoExpressionIsNull) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({"."}));
  EXPECT_THAT(cmd.roots, ElementsAre("."));
  EXPECT_THAT(cmd.expression, IsNull());
}

TEST_F(ParserTest, OrIsLowerThanImplicitAnd) {
  // `-type f -name x -o -name y` => Or( And(-type f, -name x), -name y )
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-type", "f", "-name", "x", "-o", "-name", "y"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kOr);
  EXPECT_THAT(root.lhs->kind, Expr::Kind::kAnd);
  ASSERT_THAT(root.rhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.rhs->descriptor->name, "-name");
}

TEST_F(ParserTest, NotBindsTightest) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "!", "-type", "d"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kNot);
  ASSERT_THAT(root.lhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.lhs->descriptor->name, "-type");
}

TEST_F(ParserTest, ParensGroup) {
  // `( -type f -o -type d ) -print` => And( Or(...), -print )
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "(", "-type", "f", "-o", "-type", "d", ")", "-print"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kAnd);
  EXPECT_THAT(root.lhs->kind, Expr::Kind::kOr);
  ASSERT_THAT(root.rhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.rhs->descriptor->name, "-print");
}

TEST_F(ParserTest, CommaIsLowestPrecedence) {
  // `-type f -o -type d , -name x` => Comma( Or(-type f, -type d), -name x )
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-type", "f", "-o", "-type", "d", ",", "-name", "x"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kComma);
  EXPECT_THAT(root.lhs->kind, Expr::Kind::kOr);
  ASSERT_THAT(root.rhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.rhs->descriptor->name, "-name");
}

TEST_F(ParserTest, ExecCollectsCommandUntilSemicolon) {
  // `-exec echo {} ; -print` => And( -exec[echo, {}], -print ); the ';' is consumed.
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-exec", "echo", "{}", ";", "-print"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kAnd);
  ASSERT_THAT(root.lhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.lhs->descriptor->name, "-exec");
  EXPECT_THAT(root.lhs->args, ElementsAre("echo", "{}"));
  EXPECT_THAT(root.rhs->descriptor->name, "-print");
}

TEST_F(ParserTest, ExecWithoutTerminatorErrors) {
  EXPECT_THAT(Parse({".", "-exec", "echo", "{}"}), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ParserTest, ExecPlusMarksBatchAndKeepsCommand) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-exec", "echo", "{}", "+"}));
  const Expr& root = *cmd.expression;
  EXPECT_THAT(root.descriptor->name, "-exec");
  EXPECT_TRUE(root.exec_batch);
  EXPECT_THAT(root.args, ElementsAre("echo", "{}"));
}

TEST_F(ParserTest, ExecSemicolonIsNotBatch) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-exec", "echo", "{}", ";"}));
  EXPECT_FALSE(cmd.expression->exec_batch);
}

TEST_F(ParserTest, ExecPlusRequiresTrailingBrace) {
  EXPECT_THAT(Parse({".", "-exec", "echo", "+"}), StatusIs(absl::StatusCode::kInvalidArgument));  // no {}
  EXPECT_THAT(
      Parse({".", "-exec", "echo", "{}", "x", "+"}), StatusIs(absl::StatusCode::kInvalidArgument));  // {} not last
}

TEST_F(ParserTest, ExecdirPlusMarksBatch) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-execdir", "echo", "{}", "+"}));
  EXPECT_THAT(cmd.expression->descriptor->name, "-execdir");
  EXPECT_TRUE(cmd.expression->exec_batch);
}

TEST_F(ParserTest, OkPlusNotSupported) {
  // The interactive -ok/-okdir never take the '+' batch form.
  EXPECT_THAT(Parse({".", "-ok", "echo", "{}", "+"}), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ParserTest, CaptureCollectsNameRegexAndCommand) {
  // -capture=NAME[=REGEX] cmd... ; => args = [NAME, REGEX (may be empty), cmd...].
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-capture=lines", "wc", "-l", "{}", ";", "-print"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kAnd);
  ASSERT_THAT(root.lhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.lhs->descriptor->name, "-capture");
  EXPECT_THAT(root.lhs->args, ElementsAre("lines", "", "wc", "-l", "{}"));  // empty regex slot
  EXPECT_THAT(root.rhs->descriptor->name, "-print");
}

TEST_F(ParserTest, CaptureExtractionRegexInSpec) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-capture=n=([0-9]+)", "wc", ";"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.descriptor->name, "-capture");
  EXPECT_THAT(root.args, ElementsAre("n", "([0-9]+)", "wc"));  // NAME, REGEX, command
}

TEST_F(ParserTest, CaptureErrors) {
  using ::absl::StatusCode;
  EXPECT_THAT(Parse({".", "-capture=x", "echo"}), StatusIs(StatusCode::kInvalidArgument));      // no ';'
  EXPECT_THAT(Parse({".", "-capture=", "echo", ";"}), StatusIs(StatusCode::kInvalidArgument));  // no NAME
  EXPECT_THAT(Parse({".", "-capture=x", ";"}), StatusIs(StatusCode::kInvalidArgument));         // no command
  EXPECT_THAT(Parse({".", "-capture", "echo", ";"}), StatusIs(StatusCode::kInvalidArgument));   // bare, missing =NAME
}

TEST_F(ParserTest, Errors) {
  using ::absl::StatusCode;
  EXPECT_THAT(Parse({".", "-bogus"}), StatusIs(StatusCode::kInvalidArgument));              // unknown predicate
  EXPECT_THAT(Parse({".", "-name"}), StatusIs(StatusCode::kInvalidArgument));               // missing argument
  EXPECT_THAT(Parse({".", "(", "-type", "f"}), StatusIs(StatusCode::kInvalidArgument));     // unbalanced '('
  EXPECT_THAT(Parse({".", "-o", "-type", "f"}), StatusIs(StatusCode::kInvalidArgument));    // leading operator
  EXPECT_THAT(Parse({".", "-xor", "-name", "x"}), StatusIs(StatusCode::kInvalidArgument));  // leading xff operator
  EXPECT_THAT(Parse({".", "-name", "x", "-nor"}), StatusIs(StatusCode::kInvalidArgument));  // operator missing rhs
}

TEST_F(ParserTest, ResolveCaseModeDefaultsAndFlags) {
  // Style defaults: find/xff sensitive, the opinionated styles (xfd/rg) smart.
  EXPECT_THAT(ResolveCaseMode({}, registry::Style::kFind), CaseMode::kSensitive);
  EXPECT_THAT(ResolveCaseMode({}, registry::Style::kXff), CaseMode::kSensitive);
  EXPECT_THAT(ResolveCaseMode({}, registry::Style::kXfd), CaseMode::kSmart);
  EXPECT_THAT(ResolveCaseMode({}, registry::Style::kRg), CaseMode::kSmart);
  // Flags override the default; last occurrence wins.
  EXPECT_THAT(ResolveCaseMode({"-i"}, registry::Style::kFind), CaseMode::kInsensitive);
  EXPECT_THAT(ResolveCaseMode({"-s"}, registry::Style::kFind), CaseMode::kSmart);
  EXPECT_THAT(ResolveCaseMode({"-s+"}, registry::Style::kFind), CaseMode::kSmart);
  EXPECT_THAT(ResolveCaseMode({"-s-"}, registry::Style::kXfd), CaseMode::kSensitive);
  EXPECT_THAT(ResolveCaseMode({"--case=insensitive"}, registry::Style::kFind), CaseMode::kInsensitive);
  EXPECT_THAT(ResolveCaseMode({"--case=smart"}, registry::Style::kFind), CaseMode::kSmart);
  EXPECT_THAT(ResolveCaseMode({"--case=sensitive"}, registry::Style::kXfd), CaseMode::kSensitive);
  EXPECT_THAT(ResolveCaseMode({"-i", "-s-"}, registry::Style::kFind), CaseMode::kSensitive);  // last wins
}

TEST_F(ParserTest, ApplyCaseModeFoldsSensitiveMatchers) {
  // smart: an all-lowercase glob folds; an uppercase-bearing pattern stays exact.
  ASSERT_OK_AND_ASSIGN(Command lower, Parse({".", "-name", "readme"}));
  ApplyCaseMode(lower, CaseMode::kSmart);
  EXPECT_TRUE(lower.expression->case_fold);
  ASSERT_OK_AND_ASSIGN(Command upper, Parse({".", "-name", "README"}));
  ApplyCaseMode(upper, CaseMode::kSmart);
  EXPECT_FALSE(upper.expression->case_fold);
  // insensitive: folds regardless of pattern case.
  ASSERT_OK_AND_ASSIGN(Command ins, Parse({".", "-name", "README"}));
  ApplyCaseMode(ins, CaseMode::kInsensitive);
  EXPECT_TRUE(ins.expression->case_fold);
  // The -i variant already folds (descriptor.fold_case), so it is left untouched.
  ASSERT_OK_AND_ASSIGN(Command iname, Parse({".", "-iname", "README"}));
  ApplyCaseMode(iname, CaseMode::kInsensitive);
  EXPECT_FALSE(iname.expression->case_fold);
  // sensitive is a no-op.
  ASSERT_OK_AND_ASSIGN(Command sens, Parse({".", "-name", "readme"}));
  ApplyCaseMode(sens, CaseMode::kSensitive);
  EXPECT_FALSE(sens.expression->case_fold);
}

TEST_F(ParserTest, ApplyCaseModeRecompilesRegexInsensitive) {
  // A -regex node's pre-compiled matcher is recompiled case-insensitively under smart
  // (all-lowercase pattern), so it then matches an uppercase path.
  ASSERT_OK_AND_ASSIGN(Command cmd, Parse({".", "-regex", ".*readme.*"}));
  ASSERT_THAT(cmd.expression->matcher, NotNull());
  EXPECT_FALSE(cmd.expression->matcher->PartialMatch("/x/README.txt"));  // sensitive before
  ApplyCaseMode(cmd, CaseMode::kSmart);
  ASSERT_THAT(cmd.expression->matcher, NotNull());
  EXPECT_TRUE(cmd.expression->matcher->PartialMatch("/x/README.txt"));  // folded after
}

TEST_F(ParserTest, EnforceStyleRejectsXffExtensionUnderFind) {
  // The strict find style (--config=find) refuses an xff-only primary, naming it
  // and pointing at the escape hatch.
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-println"}));
  const absl::Status status = EnforceStyle(cmd, registry::Style::kFind);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(status.message(), HasSubstr("-println"));
  EXPECT_THAT(status.message(), HasSubstr("--config=xff"));
}

TEST_F(ParserTest, EnforceStyleRejectsFileWritingLineEndingActionsUnderFind) {
  // -fprintln / -fprintfln are the file-writing counterparts of -println / -printfln,
  // so they are xff extensions the find style rejects (their bases -fprint / -fprintf
  // stay find-native).
  ASSERT_OK_AND_ASSIGN(const Command ln, Parse({".", "-fprintln", "out"}));
  EXPECT_THAT(EnforceStyle(ln, registry::Style::kFind), StatusIs(absl::StatusCode::kInvalidArgument));
  ASSERT_OK_AND_ASSIGN(const Command fln, Parse({".", "-fprintfln", "out", "%p"}));
  EXPECT_THAT(EnforceStyle(fln, registry::Style::kFind), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(EnforceStyle(ln, registry::Style::kXff), IsOk());
  EXPECT_THAT(EnforceStyle(fln, registry::Style::kXff), IsOk());
}

TEST_F(ParserTest, EnforceStyleRejectsPrintfFieldEscapeUnderFind) {
  // -printf / -fprintf are find-native, but their xff `%{field}` escape is not: the strict
  // find style rejects a format that uses it (while a plain % format stays fine). -fprintf
  // takes FILE then FORMAT, so the escape is checked in its second argument.
  ASSERT_OK_AND_ASSIGN(const Command pf, Parse({".", "-printf", "%{relpath}\n"}));
  EXPECT_THAT(EnforceStyle(pf, registry::Style::kFind), StatusIs(absl::StatusCode::kInvalidArgument));
  ASSERT_OK_AND_ASSIGN(const Command fpf, Parse({".", "-fprintf", "out", "%{name}"}));
  EXPECT_THAT(EnforceStyle(fpf, registry::Style::kFind), StatusIs(absl::StatusCode::kInvalidArgument));
  ASSERT_OK_AND_ASSIGN(const Command plain, Parse({".", "-printf", "%p\n"}));
  EXPECT_THAT(EnforceStyle(plain, registry::Style::kFind), IsOk());
  EXPECT_THAT(EnforceStyle(pf, registry::Style::kXff), IsOk());
}

TEST_F(ParserTest, EnforceStyleWalksTheWholeTree) {
  // A -capture buried under operators is still found (the check is a full walk).
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-type", "f", "-o", "-capture=n", "wc", ";"}));
  const absl::Status status = EnforceStyle(cmd, registry::Style::kFind);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(status.message(), HasSubstr("-capture"));
}

TEST_F(ParserTest, EnforceStyleAcceptsFindVocabularyUnderFind) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-type", "f", "-o", "-name", "x"}));
  EXPECT_THAT(EnforceStyle(cmd, registry::Style::kFind), IsOk());
}

TEST_F(ParserTest, EnforceStyleAcceptsXffExtensionsUnderXff) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-println"}));
  EXPECT_THAT(EnforceStyle(cmd, registry::Style::kXff), IsOk());
}

TEST_F(ParserTest, EnforceStyleAllowsAnEmptyExpression) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({"."}));  // roots only, no expression
  EXPECT_THAT(EnforceStyle(cmd, registry::Style::kFind), IsOk());
}

TEST_F(ParserTest, EnforceStyleRejectsTimeDurationValueUnderFind) {
  // The xff word/compound duration value of a day-time predicate is refused by the
  // strict find style (the bare day count and BSD unit suffix stay allowed below).
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-mtime", "-3 weeks 3 hours"}));
  const absl::Status status = EnforceStyle(cmd, registry::Style::kFind);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(status.message(), HasSubstr("-mtime"));
  EXPECT_THAT(status.message(), HasSubstr("--config=xff"));
}

TEST_F(ParserTest, EnforceStyleAcceptsTimeDurationValueUnderXff) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-mtime", "-3 weeks 3 hours"}));
  EXPECT_THAT(EnforceStyle(cmd, registry::Style::kXff), IsOk());
}

TEST_F(ParserTest, EnforceStyleAcceptsBareAndSuffixTimeUnderFind) {
  // A bare day count (POSIX/GNU) and a BSD unit suffix carry no space, so both
  // stay find-compatible under --config=find.
  ASSERT_OK_AND_ASSIGN(const Command bare, Parse({".", "-mtime", "+2"}));
  EXPECT_THAT(EnforceStyle(bare, registry::Style::kFind), IsOk());
  ASSERT_OK_AND_ASSIGN(const Command suffix, Parse({".", "-mtime", "-1h"}));
  EXPECT_THAT(EnforceStyle(suffix, registry::Style::kFind), IsOk());
}

TEST_F(ParserTest, RegexPredicatesCompileAMatcherAtParseTime) {
  // -regex carries a compiled matcher on the node (so evaluation is a lock-free read).
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-regex", ".*\\.txt"}));
  ASSERT_THAT(cmd.expression, NotNull());
  ASSERT_THAT(cmd.expression->matcher, NotNull());
  EXPECT_TRUE(cmd.expression->matcher->FullMatch("a/b.txt"));
  EXPECT_FALSE(cmd.expression->matcher->FullMatch("a/b.md"));
}

TEST_F(ParserTest, IregexMatcherFoldsCaseFromTheDescriptor) {
  // -iregex's case-insensitivity comes from the descriptor's fold_case, not a name check.
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-iregex", ".*readme"}));
  ASSERT_THAT(cmd.expression->matcher, NotNull());
  EXPECT_TRUE(cmd.expression->matcher->FullMatch("docs/README"));
}

TEST_F(ParserTest, NonRegexAndUncompilablePatternsLeaveMatcherNull) {
  ASSERT_OK_AND_ASSIGN(const Command name, Parse({".", "-name", "x"}));
  EXPECT_THAT(name.expression->matcher, IsNull());  // not a regex predicate
  ASSERT_OK_AND_ASSIGN(const Command bad, Parse({".", "-regex", "a("}));
  EXPECT_THAT(bad.expression->matcher, IsNull());  // uncompilable: null (evaluated as no-match), no parse error
}

TEST_F(ParserTest, XorBindsTighterThanOr) {
  // `-name a -xor -name b -o -name c` => Or( Xor(a, b), c ): XOR is above OR.
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-name", "a", "-xor", "-name", "b", "-o", "-name", "c"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kOr);
  EXPECT_THAT(root.lhs->kind, Expr::Kind::kXor);
  ASSERT_THAT(root.rhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.rhs->descriptor->name, "-name");
}

TEST_F(ParserTest, XorBindsLooserThanAnd) {
  // `-name a -xor -name b -name c` => Xor( a, And(b, c) ): implicit AND is above XOR.
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-name", "a", "-xor", "-name", "b", "-name", "c"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kXor);
  EXPECT_THAT(root.lhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.rhs->kind, Expr::Kind::kAnd);
}

TEST_F(ParserTest, NandBindsAtTheAndTier) {
  // `-name a -nand -name b -o -name c` => Or( Nand(a, b), c ).
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-name", "a", "-nand", "-name", "b", "-o", "-name", "c"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kOr);
  EXPECT_THAT(root.lhs->kind, Expr::Kind::kNand);
}

TEST_F(ParserTest, NorBindsAtTheOrTier) {
  // `-name a -o -name b -nor -name c` => Nor( Or(a, b), c ): left-associative at the OR tier.
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-name", "a", "-o", "-name", "b", "-nor", "-name", "c"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kNor);
  EXPECT_THAT(root.lhs->kind, Expr::Kind::kOr);
}

TEST_F(ParserTest, XnorParsesAsItsOwnNode) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-name", "a", "-xnor", "-name", "b"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kXnor);
  EXPECT_THAT(root.lhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.rhs->kind, Expr::Kind::kPredicate);
}

TEST_F(ParserTest, EnforceStyleRejectsXffOperatorsUnderFind) {
  // The new logical operators are xff extensions; the strict find style refuses
  // them even though they are interior nodes with no descriptor.
  for (const char* const op : {"-xor", "-nand", "-nor", "-xnor"}) {
    ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-name", "a", op, "-name", "b"}));
    const absl::Status status = EnforceStyle(cmd, registry::Style::kFind);
    EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument)) << op;
    EXPECT_THAT(status.message(), HasSubstr(op)) << op;
    EXPECT_THAT(status.message(), HasSubstr("--config=xff")) << op;
  }
}

TEST_F(ParserTest, EnforceStyleAcceptsXffOperatorsUnderXff) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-name", "a", "-xor", "-name", "b"}));
  EXPECT_THAT(EnforceStyle(cmd, registry::Style::kXff), IsOk());
}

}  // namespace
}  // namespace xff::parser
