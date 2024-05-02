// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "common/ast_rewrite.h"

#include <string>
#include <vector>

#include "google/api/expr/v1alpha1/syntax.pb.h"
#include "google/protobuf/text_format.h"
#include "base/ast_internal/expr.h"
#include "common/ast_visitor.h"
#include "extensions/protobuf/ast_converters.h"
#include "internal/testing.h"
#include "parser/parser.h"

namespace cel {

namespace {

using ast_internal::ParsedExpr;
using ::cel::extensions::internal::ConvertProtoExprToNative;
using ::cel::extensions::internal::ConvertProtoParsedExprToNative;
using testing::_;
using testing::ElementsAre;
using testing::InSequence;

class MockAstRewriter : public AstRewriter {
 public:
  // Expr handler.
  MOCK_METHOD(void, PreVisitExpr, (const Expr* expr), (override));

  // Expr handler.
  MOCK_METHOD(void, PostVisitExpr, (const Expr* expr), (override));

  MOCK_METHOD(void, PostVisitConst,
              (const Constant* const_expr, const Expr* expr), (override));

  // Ident node handler.
  MOCK_METHOD(void, PostVisitIdent,
              (const IdentExpr* ident_expr, const Expr* expr), (override));

  // Select node handler group
  MOCK_METHOD(void, PreVisitSelect,
              (const SelectExpr* select_expr, const Expr* expr), (override));

  MOCK_METHOD(void, PostVisitSelect,
              (const SelectExpr* select_expr, const Expr* expr), (override));

  // Call node handler group
  MOCK_METHOD(void, PreVisitCall, (const CallExpr* call_expr, const Expr* expr),
              (override));
  MOCK_METHOD(void, PostVisitCall,
              (const CallExpr* call_expr, const Expr* expr), (override));

  // Comprehension node handler group
  MOCK_METHOD(void, PreVisitComprehension,
              (const ComprehensionExpr* comprehension_expr, const Expr* expr),
              (override));
  MOCK_METHOD(void, PostVisitComprehension,
              (const ComprehensionExpr* comprehension_expr, const Expr* expr),
              (override));

  // Comprehension node handler group
  MOCK_METHOD(void, PreVisitComprehensionSubexpression,
              (const Expr* expr, const ComprehensionExpr* comprehension_expr,
               ComprehensionArg comprehension_arg),
              (override));
  MOCK_METHOD(void, PostVisitComprehensionSubexpression,
              (const Expr* expr, const ComprehensionExpr* comprehension_expr,
               ComprehensionArg comprehension_arg),
              (override));

  // We provide finer granularity for Call and Comprehension node callbacks
  // to allow special handling for short-circuiting.
  MOCK_METHOD(void, PostVisitTarget, (const Expr* expr), (override));
  MOCK_METHOD(void, PostVisitArg, (int arg_num, const Expr* expr), (override));

  // List node handler group
  MOCK_METHOD(void, PostVisitList,
              (const ListExpr* list_expr, const Expr* expr), (override));

  // Struct node handler group
  MOCK_METHOD(void, PostVisitStruct,
              (const StructExpr* struct_expr, const Expr* expr), (override));

  // Map node handler group
  MOCK_METHOD(void, PostVisitMap, (const MapExpr* map_expr, const Expr* expr),
              (override));

  MOCK_METHOD(bool, PreVisitRewrite, (Expr * expr), (override));

  MOCK_METHOD(bool, PostVisitRewrite, (Expr * expr), (override));

  MOCK_METHOD(void, TraversalStackUpdate, (absl::Span<const Expr*> path),
              (override));
};

TEST(AstCrawlerTest, CheckCrawlConstant) {
  MockAstRewriter handler;

  Expr expr;
  auto& const_expr = expr.mutable_const_expr();

  EXPECT_CALL(handler, PostVisitConst(&const_expr, &expr)).Times(1);

  AstRewrite(&expr, &handler);
}

TEST(AstCrawlerTest, CheckCrawlIdent) {
  MockAstRewriter handler;

  Expr expr;
  auto& ident_expr = expr.mutable_ident_expr();

  EXPECT_CALL(handler, PostVisitIdent(&ident_expr, &expr)).Times(1);

  AstRewrite(&expr, &handler);
}

// Test handling of Select node when operand is not set.
TEST(AstCrawlerTest, CheckCrawlSelectNotCrashingPostVisitAbsentOperand) {
  MockAstRewriter handler;

  Expr expr;
  auto& select_expr = expr.mutable_select_expr();

  // Lowest level entry will be called first
  EXPECT_CALL(handler, PostVisitSelect(&select_expr, &expr)).Times(1);

  AstRewrite(&expr, &handler);
}

// Test handling of Select node
TEST(AstCrawlerTest, CheckCrawlSelect) {
  MockAstRewriter handler;

  Expr expr;
  auto& select_expr = expr.mutable_select_expr();
  auto& operand = select_expr.mutable_operand();
  auto& ident_expr = operand.mutable_ident_expr();

  testing::InSequence seq;

  // Lowest level entry will be called first
  EXPECT_CALL(handler, PostVisitIdent(&ident_expr, &operand)).Times(1);
  EXPECT_CALL(handler, PostVisitSelect(&select_expr, &expr)).Times(1);

  AstRewrite(&expr, &handler);
}

// Test handling of Call node without receiver
TEST(AstCrawlerTest, CheckCrawlCallNoReceiver) {
  MockAstRewriter handler;

  // <call>(<const>, <ident>)
  Expr expr;
  auto& call_expr = expr.mutable_call_expr();
  call_expr.mutable_args().reserve(2);
  Expr& arg0 = call_expr.mutable_args().emplace_back();
  auto& const_expr = arg0.mutable_const_expr();
  Expr& arg1 = call_expr.mutable_args().emplace_back();
  auto& ident_expr = arg1.mutable_ident_expr();

  testing::InSequence seq;

  // Lowest level entry will be called first
  EXPECT_CALL(handler, PreVisitCall(&call_expr, &expr)).Times(1);
  EXPECT_CALL(handler, PostVisitTarget(_)).Times(0);

  // Arg0
  EXPECT_CALL(handler, PostVisitConst(&const_expr, &arg0)).Times(1);
  EXPECT_CALL(handler, PostVisitExpr(&arg0)).Times(1);
  EXPECT_CALL(handler, PostVisitArg(0, &expr)).Times(1);

  // Arg1
  EXPECT_CALL(handler, PostVisitIdent(&ident_expr, &arg1)).Times(1);
  EXPECT_CALL(handler, PostVisitExpr(&arg1)).Times(1);
  EXPECT_CALL(handler, PostVisitArg(1, &expr)).Times(1);

  // Back to call
  EXPECT_CALL(handler, PostVisitCall(&call_expr, &expr)).Times(1);
  EXPECT_CALL(handler, PostVisitExpr(&expr)).Times(1);

  AstRewrite(&expr, &handler);
}

// Test handling of Call node with receiver
TEST(AstCrawlerTest, CheckCrawlCallReceiver) {
  MockAstRewriter handler;

  // <ident>.<call>(<const>, <ident>)
  Expr expr;
  auto& call_expr = expr.mutable_call_expr();
  Expr& target = call_expr.mutable_target();
  auto& target_ident = target.mutable_ident_expr();
  call_expr.mutable_args().reserve(2);
  Expr& arg0 = call_expr.mutable_args().emplace_back();
  auto& const_expr = arg0.mutable_const_expr();
  Expr& arg1 = call_expr.mutable_args().emplace_back();
  auto& ident_expr = arg1.mutable_ident_expr();

  testing::InSequence seq;

  // Lowest level entry will be called first
  EXPECT_CALL(handler, PreVisitCall(&call_expr, &expr)).Times(1);

  // Target
  EXPECT_CALL(handler, PostVisitIdent(&target_ident, &target)).Times(1);
  EXPECT_CALL(handler, PostVisitExpr(&target)).Times(1);
  EXPECT_CALL(handler, PostVisitTarget(&expr)).Times(1);

  // Arg0
  EXPECT_CALL(handler, PostVisitConst(&const_expr, &arg0)).Times(1);
  EXPECT_CALL(handler, PostVisitExpr(&arg0)).Times(1);
  EXPECT_CALL(handler, PostVisitArg(0, &expr)).Times(1);

  // Arg1
  EXPECT_CALL(handler, PostVisitIdent(&ident_expr, &arg1)).Times(1);
  EXPECT_CALL(handler, PostVisitExpr(&arg1)).Times(1);
  EXPECT_CALL(handler, PostVisitArg(1, &expr)).Times(1);

  // Back to call
  EXPECT_CALL(handler, PostVisitCall(&call_expr, &expr)).Times(1);
  EXPECT_CALL(handler, PostVisitExpr(&expr)).Times(1);

  AstRewrite(&expr, &handler);
}

// Test handling of Comprehension node
TEST(AstCrawlerTest, CheckCrawlComprehension) {
  MockAstRewriter handler;

  Expr expr;
  auto& c = expr.mutable_comprehension_expr();
  auto& iter_range = c.mutable_iter_range();
  auto& iter_range_expr = iter_range.mutable_const_expr();
  auto& accu_init = c.mutable_accu_init();
  auto& accu_init_expr = accu_init.mutable_ident_expr();
  auto& loop_condition = c.mutable_loop_condition();
  auto& loop_condition_expr = loop_condition.mutable_const_expr();
  auto& loop_step = c.mutable_loop_step();
  auto& loop_step_expr = loop_step.mutable_ident_expr();
  auto& result = c.mutable_result();
  auto& result_expr = result.mutable_const_expr();

  testing::InSequence seq;

  // Lowest level entry will be called first
  EXPECT_CALL(handler, PreVisitComprehension(&c, &expr)).Times(1);

  EXPECT_CALL(handler,
              PreVisitComprehensionSubexpression(&iter_range, &c, ITER_RANGE))
      .Times(1);
  EXPECT_CALL(handler, PostVisitConst(&iter_range_expr, &iter_range)).Times(1);
  EXPECT_CALL(handler,
              PostVisitComprehensionSubexpression(&iter_range, &c, ITER_RANGE))
      .Times(1);

  // ACCU_INIT
  EXPECT_CALL(handler,
              PreVisitComprehensionSubexpression(&accu_init, &c, ACCU_INIT))
      .Times(1);
  EXPECT_CALL(handler, PostVisitIdent(&accu_init_expr, &accu_init)).Times(1);
  EXPECT_CALL(handler,
              PostVisitComprehensionSubexpression(&accu_init, &c, ACCU_INIT))
      .Times(1);

  // LOOP CONDITION
  EXPECT_CALL(handler, PreVisitComprehensionSubexpression(&loop_condition, &c,
                                                          LOOP_CONDITION))
      .Times(1);
  EXPECT_CALL(handler, PostVisitConst(&loop_condition_expr, &loop_condition))
      .Times(1);
  EXPECT_CALL(handler, PostVisitComprehensionSubexpression(&loop_condition, &c,
                                                           LOOP_CONDITION))
      .Times(1);

  // LOOP STEP
  EXPECT_CALL(handler,
              PreVisitComprehensionSubexpression(&loop_step, &c, LOOP_STEP))
      .Times(1);
  EXPECT_CALL(handler, PostVisitIdent(&loop_step_expr, &loop_step)).Times(1);
  EXPECT_CALL(handler,
              PostVisitComprehensionSubexpression(&loop_step, &c, LOOP_STEP))
      .Times(1);

  // RESULT
  EXPECT_CALL(handler, PreVisitComprehensionSubexpression(&result, &c, RESULT))
      .Times(1);

  EXPECT_CALL(handler, PostVisitConst(&result_expr, &result)).Times(1);

  EXPECT_CALL(handler, PostVisitComprehensionSubexpression(&result, &c, RESULT))
      .Times(1);

  EXPECT_CALL(handler, PostVisitComprehension(&c, &expr)).Times(1);

  RewriteTraversalOptions opts;
  opts.use_comprehension_callbacks = true;
  AstRewrite(&expr, &handler, opts);
}

// Test handling of Comprehension node
TEST(AstCrawlerTest, CheckCrawlComprehensionLegacyCallbacks) {
  MockAstRewriter handler;

  Expr expr;
  auto& c = expr.mutable_comprehension_expr();
  auto& iter_range = c.mutable_iter_range();
  auto& iter_range_expr = iter_range.mutable_const_expr();
  auto& accu_init = c.mutable_accu_init();
  auto& accu_init_expr = accu_init.mutable_ident_expr();
  auto& loop_condition = c.mutable_loop_condition();
  auto& loop_condition_expr = loop_condition.mutable_const_expr();
  auto& loop_step = c.mutable_loop_step();
  auto& loop_step_expr = loop_step.mutable_ident_expr();
  auto& result = c.mutable_result();
  auto& result_expr = result.mutable_const_expr();

  testing::InSequence seq;

  // Lowest level entry will be called first
  EXPECT_CALL(handler, PreVisitComprehension(&c, &expr)).Times(1);

  EXPECT_CALL(handler, PostVisitConst(&iter_range_expr, &iter_range)).Times(1);
  EXPECT_CALL(handler, PostVisitArg(ITER_RANGE, &expr)).Times(1);

  // ACCU_INIT
  EXPECT_CALL(handler, PostVisitIdent(&accu_init_expr, &accu_init)).Times(1);
  EXPECT_CALL(handler, PostVisitArg(ACCU_INIT, &expr)).Times(1);

  // LOOP CONDITION
  EXPECT_CALL(handler, PostVisitConst(&loop_condition_expr, &loop_condition))
      .Times(1);
  EXPECT_CALL(handler, PostVisitArg(LOOP_CONDITION, &expr)).Times(1);

  // LOOP STEP
  EXPECT_CALL(handler, PostVisitIdent(&loop_step_expr, &loop_step)).Times(1);
  EXPECT_CALL(handler, PostVisitArg(LOOP_STEP, &expr)).Times(1);

  // RESULT
  EXPECT_CALL(handler, PostVisitConst(&result_expr, &result)).Times(1);
  EXPECT_CALL(handler, PostVisitArg(RESULT, &expr)).Times(1);

  EXPECT_CALL(handler, PostVisitComprehension(&c, &expr)).Times(1);

  AstRewrite(&expr, &handler);
}

// Test handling of List node.
TEST(AstCrawlerTest, CheckList) {
  MockAstRewriter handler;

  Expr expr;
  auto& list_expr = expr.mutable_list_expr();
  list_expr.mutable_elements().reserve(2);
  auto& arg0 = list_expr.mutable_elements().emplace_back().mutable_expr();
  auto& const_expr = arg0.mutable_const_expr();
  auto& arg1 = list_expr.mutable_elements().emplace_back().mutable_expr();
  auto& ident_expr = arg1.mutable_ident_expr();

  testing::InSequence seq;

  EXPECT_CALL(handler, PostVisitConst(&const_expr, &arg0)).Times(1);
  EXPECT_CALL(handler, PostVisitIdent(&ident_expr, &arg1)).Times(1);
  EXPECT_CALL(handler, PostVisitList(&list_expr, &expr)).Times(1);

  AstRewrite(&expr, &handler);
}

// Test handling of Struct node.
TEST(AstCrawlerTest, CheckStruct) {
  MockAstRewriter handler;

  Expr expr;
  auto& struct_expr = expr.mutable_struct_expr();
  auto& entry0 = struct_expr.mutable_fields().emplace_back();

  auto& value = entry0.mutable_value().mutable_ident_expr();

  testing::InSequence seq;

  EXPECT_CALL(handler, PostVisitIdent(&value, &entry0.value())).Times(1);
  EXPECT_CALL(handler, PostVisitStruct(&struct_expr, &expr)).Times(1);

  AstRewrite(&expr, &handler);
}

// Test handling of Map node.
TEST(AstCrawlerTest, CheckMap) {
  MockAstRewriter handler;

  Expr expr;
  auto& map_expr = expr.mutable_map_expr();
  auto& entry0 = map_expr.mutable_entries().emplace_back();

  auto& key = entry0.mutable_key().mutable_const_expr();
  auto& value = entry0.mutable_value().mutable_ident_expr();

  testing::InSequence seq;

  EXPECT_CALL(handler, PostVisitConst(&key, &entry0.key())).Times(1);
  EXPECT_CALL(handler, PostVisitIdent(&value, &entry0.value())).Times(1);
  EXPECT_CALL(handler, PostVisitMap(&map_expr, &expr)).Times(1);

  AstRewrite(&expr, &handler);
}

// Test generic Expr handlers.
TEST(AstCrawlerTest, CheckExprHandlers) {
  MockAstRewriter handler;

  Expr expr;
  auto& map_expr = expr.mutable_map_expr();
  auto& entry0 = map_expr.mutable_entries().emplace_back();

  entry0.mutable_key().mutable_const_expr();
  entry0.mutable_value().mutable_ident_expr();

  EXPECT_CALL(handler, PreVisitExpr(_)).Times(3);
  EXPECT_CALL(handler, PostVisitExpr(_)).Times(3);

  AstRewrite(&expr, &handler);
}

// Test generic Expr handlers.
TEST(AstCrawlerTest, CheckExprRewriteHandlers) {
  MockAstRewriter handler;

  Expr select_expr;
  select_expr.mutable_select_expr().set_field("var");
  auto& inner_select_expr = select_expr.mutable_select_expr().mutable_operand();
  inner_select_expr.mutable_select_expr().set_field("mid");
  auto& ident = inner_select_expr.mutable_select_expr().mutable_operand();
  ident.mutable_ident_expr().set_name("top");

  {
    InSequence sequence;
    EXPECT_CALL(handler,
                TraversalStackUpdate(testing::ElementsAre(&select_expr)));
    EXPECT_CALL(handler, PreVisitRewrite(&select_expr));

    EXPECT_CALL(handler, TraversalStackUpdate(testing::ElementsAre(
                             &select_expr, &inner_select_expr)));
    EXPECT_CALL(handler, PreVisitRewrite(&inner_select_expr));

    EXPECT_CALL(handler, TraversalStackUpdate(testing::ElementsAre(
                             &select_expr, &inner_select_expr, &ident)));
    EXPECT_CALL(handler, PreVisitRewrite(&ident));

    EXPECT_CALL(handler, PostVisitRewrite(&ident));
    EXPECT_CALL(handler, TraversalStackUpdate(testing::ElementsAre(
                             &select_expr, &inner_select_expr)));

    EXPECT_CALL(handler, PostVisitRewrite(&inner_select_expr));
    EXPECT_CALL(handler,
                TraversalStackUpdate(testing::ElementsAre(&select_expr)));

    EXPECT_CALL(handler, PostVisitRewrite(&select_expr));
    EXPECT_CALL(handler, TraversalStackUpdate(testing::IsEmpty()));
  }

  EXPECT_FALSE(AstRewrite(&select_expr, &handler));
}

// Simple rewrite that replaces a select path with a dot-qualified identifier.
class RewriterExample : public AstRewriterBase {
 public:
  RewriterExample() {}
  bool PostVisitRewrite(Expr* expr) override {
    if (target_.has_value() && expr->id() == *target_) {
      expr->mutable_ident_expr().set_name("com.google.Identifier");
      return true;
    }
    return false;
  }

  void PostVisitIdent(const IdentExpr* ident, const Expr* expr) override {
    if (path_.size() >= 3) {
      if (ident->name() == "com") {
        const Expr* p1 = path_.at(path_.size() - 2);
        const Expr* p2 = path_.at(path_.size() - 3);

        if (p1->has_select_expr() && p1->select_expr().field() == "google" &&
            p2->has_select_expr() &&
            p2->select_expr().field() == "Identifier") {
          target_ = p2->id();
        }
      }
    }
  }

  void TraversalStackUpdate(absl::Span<const Expr*> path) override {
    path_ = path;
  }

 private:
  absl::Span<const Expr*> path_;
  absl::optional<int64_t> target_;
};

TEST(AstRewrite, SelectRewriteExample) {
  ASSERT_OK_AND_ASSIGN(
      ParsedExpr parsed,
      ConvertProtoParsedExprToNative(
          google::api::expr::parser::Parse("com.google.Identifier").value()));
  RewriterExample example;
  ASSERT_TRUE(AstRewrite(&parsed.mutable_expr(), &example));

  google::api::expr::v1alpha1::Expr expected_expr;
  google::protobuf::TextFormat::ParseFromString(
      R"pb(
        id: 3
        ident_expr { name: "com.google.Identifier" }
      )pb",
      &expected_expr);
  EXPECT_EQ(parsed.expr(), ConvertProtoExprToNative(expected_expr).value());
}

// Rewrites x -> y -> z to demonstrate traversal when a node is rewritten on
// both passes.
class PreRewriterExample : public AstRewriterBase {
 public:
  PreRewriterExample() {}
  bool PreVisitRewrite(Expr* expr) override {
    if (expr->ident_expr().name() == "x") {
      expr->mutable_ident_expr().set_name("y");
      return true;
    }
    return false;
  }

  bool PostVisitRewrite(Expr* expr) override {
    if (expr->ident_expr().name() == "y") {
      expr->mutable_ident_expr().set_name("z");
      return true;
    }
    return false;
  }

  void PostVisitIdent(const IdentExpr* ident, const Expr* expr) override {
    visited_idents_.push_back(ident->name());
  }

  const std::vector<std::string>& visited_idents() const {
    return visited_idents_;
  }

 private:
  std::vector<std::string> visited_idents_;
};

TEST(AstRewrite, PreAndPostVisitExpample) {
  ASSERT_OK_AND_ASSIGN(ParsedExpr parsed,
                       ConvertProtoParsedExprToNative(
                           google::api::expr::parser::Parse("x").value()));
  PreRewriterExample visitor;
  ASSERT_TRUE(AstRewrite(&parsed.mutable_expr(), &visitor));

  google::api::expr::v1alpha1::Expr expected_expr;
  google::protobuf::TextFormat::ParseFromString(
      R"pb(
        id: 1
        ident_expr { name: "z" }
      )pb",
      &expected_expr);
  EXPECT_EQ(parsed.expr(), ConvertProtoExprToNative(expected_expr).value());
  EXPECT_THAT(visitor.visited_idents(), ElementsAre("y"));
}

}  // namespace

}  // namespace cel