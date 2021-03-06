#include "eval/eval/const_value_step.h"
#include "eval/eval/expression_step_base.h"
#include "google/protobuf/duration.pb.h"
#include "google/protobuf/timestamp.pb.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

using google::api::expr::v1alpha1::Constant;
using google::api::expr::v1alpha1::Expr;

namespace {

class ConstValueStep : public ExpressionStepBase {
 public:
  ConstValueStep(const Expr* expr, const CelValue& value, bool comes_from_ast)
      : ExpressionStepBase(expr, comes_from_ast), value_(value) {}

  util::Status Evaluate(ExecutionFrame* context) const override;

 private:
  CelValue value_;
};

util::Status ConstValueStep::Evaluate(ExecutionFrame* frame) const {
  frame->value_stack().Push(value_);

  return util::OkStatus();
}

}  // namespace

util::StatusOr<std::unique_ptr<ExpressionStep>> CreateConstValueStep(
    const Constant* const_expr, const Expr* expr, bool comes_from_ast) {
  CelValue value = CelValue::CreateNull();
  switch (const_expr->constant_kind_case()) {
    case Constant::kNullValue:
      value = CelValue::CreateNull();
      break;
    case Constant::kBoolValue:
      value = CelValue::CreateBool(const_expr->bool_value());
      break;
    case Constant::kInt64Value:
      value = CelValue::CreateInt64(const_expr->int64_value());
      break;
    case Constant::kUint64Value:
      value = CelValue::CreateUint64(const_expr->uint64_value());
      break;
    case Constant::kDoubleValue:
      value = CelValue::CreateDouble(const_expr->double_value());
      break;
    case Constant::kStringValue:
      value = CelValue::CreateString(&const_expr->string_value());
      break;
    case Constant::kBytesValue:
      value = CelValue::CreateBytes(&const_expr->bytes_value());
      break;
    case Constant::kDurationValue:
      value = CelValue::CreateDuration(&const_expr->duration_value());
      break;
    case Constant::kTimestampValue:
      value = CelValue::CreateTimestamp(&const_expr->timestamp_value());
      break;
    default:
      return util::MakeStatus(google::rpc::Code::INVALID_ARGUMENT,
                          "Unsupported constant type");
      break;
  }

  std::unique_ptr<ExpressionStep> step =
      absl::make_unique<ConstValueStep>(expr, value, comes_from_ast);
  return std::move(step);
}

// Factory method for Constant(Enum value) - based Execution step
util::StatusOr<std::unique_ptr<ExpressionStep>> CreateConstValueStep(
    const google::protobuf::EnumValueDescriptor* value_descriptor, const Expr* expr) {
  CelValue value = CelValue::CreateInt64(value_descriptor->number());

  std::unique_ptr<ExpressionStep> step =
      absl::make_unique<ConstValueStep>(expr, value, false);
  return std::move(step);
}

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google
