#include "eval/compiler/flat_expr_builder.h"

#include "google/api/expr/v1alpha1/syntax.pb.h"
#include "google/protobuf/field_mask.pb.h"
#include "google/protobuf/text_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_split.h"
#include "eval/proto/cel_error.pb.h"
#include "eval/public/builtin_func_registrar.h"
#include "eval/testutil/test_message.pb.h"
namespace google {
namespace api {
namespace expr {
namespace runtime {

namespace {

using google::api::expr::v1alpha1::Expr;
using google::api::expr::v1alpha1::SourceInfo;

using google::protobuf::FieldMask;
using testing::Eq;

class ConcatFunction : public CelFunction {
 public:
  explicit ConcatFunction() : CelFunction(CreateDescriptor()) {}

  static CelFunction::Descriptor CreateDescriptor() {
    return Descriptor{
        "concat", false, {CelValue::Type::kString, CelValue::Type::kString}};
  }

  util::Status Evaluate(absl::Span<const CelValue> args, CelValue* result,
                        google::protobuf::Arena* arena) const override {
    if (args.size() != 2) {
      return util::MakeStatus(google::rpc::Code::INVALID_ARGUMENT,
                          "Bad arguments number");
    }

    std::string concat = std::string(args[0].StringOrDie().value()) +
                    std::string(args[1].StringOrDie().value());

    auto* concatenated =
        google::protobuf::Arena::Create<std::string>(arena, std::move(concat));

    *result = CelValue::CreateString(concatenated);

    return util::OkStatus();
  }
};

TEST(FlatExprBuilderTest, SimpleEndToEnd) {
  Expr expr;
  SourceInfo source_info;
  auto call_expr = expr.mutable_call_expr();
  call_expr->set_function("concat");

  auto arg1 = call_expr->add_args();
  arg1->mutable_const_expr()->set_string_value("prefix");

  auto arg2 = call_expr->add_args();
  arg2->mutable_ident_expr()->set_name("value");

  FlatExprBuilder builder;

  auto register_status =
      builder.GetRegistry()->Register(absl::make_unique<ConcatFunction>());
  ASSERT_TRUE(util::IsOk(register_status));

  auto build_status = builder.CreateExpression(&expr, &source_info);
  ASSERT_TRUE(util::IsOk(build_status));

  auto cel_expr = std::move(build_status.ValueOrDie());

  std::string variable = "test";

  Activation activation;
  activation.InsertValue("value", CelValue::CreateString(&variable));

  google::protobuf::Arena arena;

  auto eval_status = cel_expr->Evaluate(activation, &arena);
  ASSERT_TRUE(util::IsOk(eval_status));

  CelValue result = eval_status.ValueOrDie();

  ASSERT_TRUE(result.IsString());

  EXPECT_THAT(result.StringOrDie().value(), Eq("prefixtest"));
}

class RecorderFunction : public CelFunction {
 public:
  explicit RecorderFunction(const std::string& name, int* count)
      : CelFunction(Descriptor{name, false, {}}), count_(count) {}

  util::Status Evaluate(absl::Span<const CelValue> args, CelValue* result,
                        google::protobuf::Arena* arena) const override {
    if (!args.empty()) {
      return util::MakeStatus(google::rpc::Code::INVALID_ARGUMENT,
                          "Bad arguments number");
    }
    (*count_)++;
    *result = CelValue::CreateBool(true);
    return util::OkStatus();
  }

  int* count_;
};

TEST(FlatExprBuilderTest, Shortcircuiting) {
  Expr expr;
  SourceInfo source_info;
  auto call_expr = expr.mutable_call_expr();
  call_expr->set_function("_||_");

  auto arg1 = call_expr->add_args();
  arg1->mutable_call_expr()->set_function("recorder1");

  auto arg2 = call_expr->add_args();
  arg2->mutable_call_expr()->set_function("recorder2");

  FlatExprBuilder builder;
  auto builtin_status = RegisterBuiltinFunctions(builder.GetRegistry());

  int count1 = 0;
  int count2 = 0;

  auto register_status1 = builder.GetRegistry()->Register(
      absl::make_unique<RecorderFunction>("recorder1", &count1));
  ASSERT_TRUE(util::IsOk(register_status1));
  auto register_status2 = builder.GetRegistry()->Register(
      absl::make_unique<RecorderFunction>("recorder2", &count2));
  ASSERT_TRUE(util::IsOk(register_status2));

  // Shortcircuiting on.
  auto build_status_on = builder.CreateExpression(&expr, &source_info);
  ASSERT_TRUE(util::IsOk(build_status_on));

  auto cel_expr_on = std::move(build_status_on.ValueOrDie());

  Activation activation;
  google::protobuf::Arena arena;
  auto eval_status_on = cel_expr_on->Evaluate(activation, &arena);
  ASSERT_TRUE(util::IsOk(eval_status_on));

  EXPECT_THAT(count1, Eq(1));
  EXPECT_THAT(count2, Eq(0));

  // Shortcircuiting off.
  builder.set_shortcircuiting(false);
  auto build_status_off = builder.CreateExpression(&expr, &source_info);
  ASSERT_TRUE(util::IsOk(build_status_off));

  auto cel_expr_off = std::move(build_status_off.ValueOrDie());

  count1 = 0;
  count2 = 0;

  auto eval_status_off = cel_expr_off->Evaluate(activation, &arena);
  ASSERT_TRUE(util::IsOk(eval_status_off));

  EXPECT_THAT(count1, Eq(1));
  EXPECT_THAT(count2, Eq(1));
}

TEST(FlatExprBuilderTest, ShortcircuitingComprehension) {
  Expr expr;
  SourceInfo source_info;
  auto comprehension_expr = expr.mutable_comprehension_expr();
  comprehension_expr->set_iter_var("x");
  auto list_expr =
      comprehension_expr->mutable_iter_range()->mutable_list_expr();
  list_expr->add_elements()->mutable_const_expr()->set_int64_value(1);
  list_expr->add_elements()->mutable_const_expr()->set_int64_value(2);
  list_expr->add_elements()->mutable_const_expr()->set_int64_value(3);
  comprehension_expr->set_accu_var("accu");
  comprehension_expr->mutable_accu_init()->mutable_const_expr()->set_bool_value(
      false);
  comprehension_expr->mutable_loop_condition()
      ->mutable_const_expr()
      ->set_bool_value(false);
  comprehension_expr->mutable_loop_step()->mutable_call_expr()->set_function(
      "loop_step");
  comprehension_expr->mutable_result()->mutable_const_expr()->set_bool_value(
      false);

  FlatExprBuilder builder;
  auto builtin_status = RegisterBuiltinFunctions(builder.GetRegistry());

  int count = 0;
  auto register_status = builder.GetRegistry()->Register(
      absl::make_unique<RecorderFunction>("loop_step", &count));
  ASSERT_TRUE(util::IsOk(register_status));

  // Shortcircuiting on.
  auto build_status_on = builder.CreateExpression(&expr, &source_info);
  ASSERT_TRUE(util::IsOk(build_status_on));

  auto cel_expr_on = std::move(build_status_on.ValueOrDie());

  Activation activation;
  google::protobuf::Arena arena;
  auto eval_status_on = cel_expr_on->Evaluate(activation, &arena);
  ASSERT_TRUE(util::IsOk(eval_status_on));

  EXPECT_THAT(count, Eq(0));

  // Shortcircuiting off.
  builder.set_shortcircuiting(false);
  auto build_status_off = builder.CreateExpression(&expr, &source_info);
  ASSERT_TRUE(util::IsOk(build_status_off));

  auto cel_expr_off = std::move(build_status_off.ValueOrDie());

  count = 0;

  auto eval_status_off = cel_expr_off->Evaluate(activation, &arena);
  ASSERT_TRUE(util::IsOk(eval_status_off));

  EXPECT_THAT(count, Eq(3));
}

TEST(FlatExprBuilderTest, MapComprehension) {
  Expr expr;
  // {1: "", 2: ""}.all(x, x > 0)
  google::protobuf::TextFormat::ParseFromString(R"(
    comprehension_expr {
      iter_var: "k"
      accu_var: "accu"
      accu_init {
        const_expr { bool_value: true }
      }
      loop_condition { ident_expr { name: "accu" } }
      result { ident_expr { name: "accu" } }
      loop_step {
        call_expr {
          function: "_&&_"
          args {
            ident_expr { name: "accu" }
          }
          args {
            call_expr {
              function: "_>_"
              args { ident_expr { name: "k" } }
              args { const_expr { int64_value: 0 } }
            }
          }
        }
      }
      iter_range {
        struct_expr {
          entries {
            map_key { const_expr { int64_value: 1 } }
            value { const_expr { string_value: "" } }
          }
          entries {
            map_key { const_expr { int64_value: 2 } }
            value { const_expr { string_value: "" } }
          }
        }
      }
    })",
                                      &expr);

  FlatExprBuilder builder;
  ASSERT_TRUE(util::IsOk(RegisterBuiltinFunctions(builder.GetRegistry())));
  SourceInfo source_info;
  auto build_status = builder.CreateExpression(&expr, &source_info);
  ASSERT_TRUE(util::IsOk(build_status));

  auto cel_expr = std::move(build_status.ValueOrDie());

  Activation activation;
  google::protobuf::Arena arena;
  auto result_or = cel_expr->Evaluate(activation, &arena);
  ASSERT_TRUE(util::IsOk(result_or));
  CelValue result = result_or.ValueOrDie();
  ASSERT_TRUE(result.IsBool());
  EXPECT_TRUE(result.BoolOrDie());
}

TEST(FlatExprBuilderTest, ComprehensionWorksForError) {
  Expr expr;
  // {}[0].all(x, x) should evaluate OK but return an error value
  google::protobuf::TextFormat::ParseFromString(R"(
    id: 4
    comprehension_expr {
      iter_var: "x"
      iter_range {
        id: 2
        call_expr {
          function: "_[_]"
          args {
            id: 1
            struct_expr {
            }
          }
          args {
            id: 3
            const_expr {
              int64_value: 0
            }
          }
        }
      }
      accu_var: "__result__"
      accu_init {
        id: 7
        const_expr {
          bool_value: true
        }
      }
      loop_condition {
        id: 8
        call_expr {
          function: "__not_strictly_false__"
          args {
            id: 9
            ident_expr {
              name: "__result__"
            }
          }
        }
      }
      loop_step {
        id: 10
        call_expr {
          function: "_&&_"
          args {
            id: 11
            ident_expr {
              name: "__result__"
            }
          }
          args {
            id: 6
            ident_expr {
              name: "x"
            }
          }
        }
      }
      result {
        id: 12
        ident_expr {
          name: "__result__"
        }
      }
    })",
                                      &expr);

  FlatExprBuilder builder;
  ASSERT_TRUE(util::IsOk(RegisterBuiltinFunctions(builder.GetRegistry())));
  SourceInfo source_info;
  auto build_status = builder.CreateExpression(&expr, &source_info);
  ASSERT_TRUE(util::IsOk(build_status));

  auto cel_expr = std::move(build_status.ValueOrDie());

  Activation activation;
  google::protobuf::Arena arena;
  auto result_or = cel_expr->Evaluate(activation, &arena);
  ASSERT_TRUE(util::IsOk(result_or));
  CelValue result = result_or.ValueOrDie();
  ASSERT_TRUE(result.IsError());
}

TEST(FlatExprBuilderTest, ComprehensionWorksForNonContainer) {
  Expr expr;
  // 0.all(x, x) should evaluate OK but return an error value.
  google::protobuf::TextFormat::ParseFromString(R"(
    id: 4
    comprehension_expr {
      iter_var: "x"
      iter_range {
        id: 2
        const_expr {
          int64_value: 0
        }
      }
      accu_var: "__result__"
      accu_init {
        id: 7
        const_expr {
          bool_value: true
        }
      }
      loop_condition {
        id: 8
        call_expr {
          function: "__not_strictly_false__"
          args {
            id: 9
            ident_expr {
              name: "__result__"
            }
          }
        }
      }
      loop_step {
        id: 10
        call_expr {
          function: "_&&_"
          args {
            id: 11
            ident_expr {
              name: "__result__"
            }
          }
          args {
            id: 6
            ident_expr {
              name: "x"
            }
          }
        }
      }
      result {
        id: 12
        ident_expr {
          name: "__result__"
        }
      }
    })",
                                      &expr);

  FlatExprBuilder builder;
  ASSERT_TRUE(util::IsOk(RegisterBuiltinFunctions(builder.GetRegistry())));
  SourceInfo source_info;
  auto build_status = builder.CreateExpression(&expr, &source_info);
  ASSERT_TRUE(util::IsOk(build_status));

  auto cel_expr = std::move(build_status.ValueOrDie());

  Activation activation;
  google::protobuf::Arena arena;
  auto result_or = cel_expr->Evaluate(activation, &arena);
  ASSERT_TRUE(util::IsOk(result_or));
  CelValue result = result_or.ValueOrDie();
  ASSERT_TRUE(result.IsError());
  EXPECT_THAT(result.ErrorOrDie()->message(), Eq("no_matching_overload"));
}

TEST(FlatExprBuilderTest, UnknownSupportTest) {
  TestMessage message;

  Expr expr;
  SourceInfo source_info;

  auto select_expr = expr.mutable_select_expr();
  select_expr->set_field("int32_value");

  auto operand1 = select_expr->mutable_operand();
  auto select_expr1 = operand1->mutable_select_expr();

  select_expr1->set_field("message_value");
  auto operand2 = select_expr1->mutable_operand();

  operand2->mutable_ident_expr()->set_name("message");

  FlatExprBuilder builder;

  auto build_status = builder.CreateExpression(&expr, &source_info);
  ASSERT_TRUE(util::IsOk(build_status));

  auto cel_expr = std::move(build_status.ValueOrDie());

  message.mutable_message_value()->set_int32_value(1);

  google::protobuf::Arena arena;
  Activation activation;
  activation.InsertValue("message", CelValue::CreateMessage(&message, &arena));

  auto eval_status = cel_expr->Evaluate(activation, &arena);

  ASSERT_TRUE(util::IsOk(eval_status));
  CelValue result = eval_status.ValueOrDie();

  ASSERT_TRUE(result.IsInt64());
  EXPECT_THAT(result.Int64OrDie(), Eq(1));

  FieldMask mask;
  mask.add_paths("message.message_value.int32_value");
  activation.set_unknown_paths(mask);
  eval_status = cel_expr->Evaluate(activation, &arena);
  ASSERT_TRUE(util::IsOk(eval_status));
  result = eval_status.ValueOrDie();
  ASSERT_TRUE(result.IsError());

  mask.clear_paths();
  mask.add_paths("message.message_value");
  activation.set_unknown_paths(mask);
  eval_status = cel_expr->Evaluate(activation, &arena);
  ASSERT_TRUE(util::IsOk(eval_status));
  result = eval_status.ValueOrDie();
  ASSERT_TRUE(result.IsError());
}

TEST(FlatExprBuilderTest, SimpleEnumTest) {
  TestMessage message;

  Expr expr;
  SourceInfo source_info;

  constexpr char enum_name[] =
      "google.api.expr.runtime.TestMessage.TestEnum.TEST_ENUM_1";

  std::vector<std::string> enum_name_parts = absl::StrSplit(enum_name, '.');
  Expr* cur_expr = &expr;

  for (int i = enum_name_parts.size() - 1; i > 0; i--) {
    auto select_expr = cur_expr->mutable_select_expr();
    select_expr->set_field(enum_name_parts[i]);
    cur_expr = select_expr->mutable_operand();
  }

  cur_expr->mutable_ident_expr()->set_name(enum_name_parts[0]);

  FlatExprBuilder builder;
  builder.addResolvableEnum(TestMessage::TestEnum_descriptor());

  auto build_status = builder.CreateExpression(&expr, &source_info);
  ASSERT_TRUE(util::IsOk(build_status));

  auto cel_expr = std::move(build_status.ValueOrDie());

  google::protobuf::Arena arena;
  Activation activation;
  auto eval_status = cel_expr->Evaluate(activation, &arena);

  ASSERT_TRUE(util::IsOk(eval_status));
  CelValue result = eval_status.ValueOrDie();

  ASSERT_TRUE(result.IsInt64());
  EXPECT_THAT(result.Int64OrDie(), Eq(TestMessage::TEST_ENUM_1));
}

}  // namespace

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google
