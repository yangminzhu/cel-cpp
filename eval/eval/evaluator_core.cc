#include "eval/eval/evaluator_core.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

using google::api::expr::v1alpha1::Expr;

const ExpressionStep* ExecutionFrame::Next() {
  size_t end_pos = execution_path_->size();

  if (pc_ < end_pos) return (*execution_path_)[pc_++].get();
  if (pc_ > end_pos) {
    GOOGLE_LOG(ERROR) << "Attempting to step beyond the end of execution path.";
  }
  return nullptr;
}

util::StatusOr<CelValue> CelExpressionFlatImpl::Evaluate(
    const Activation& activation, google::protobuf::Arena* arena) const {
  return Trace(activation, arena, CelEvaluationListener());
}

util::StatusOr<CelValue> CelExpressionFlatImpl::Trace(
    const Activation& activation, google::protobuf::Arena* arena,
    CelEvaluationListener callback) const {
  ExecutionFrame frame(&path_, activation, arena);

  ValueStack* stack = &frame.value_stack();
  size_t initial_stack_size = stack->size();
  const ExpressionStep* expr;
  const Expr* current;
  while ((expr = frame.Next()) != nullptr) {
    auto status = expr->Evaluate(&frame);
    if (!util::IsOk(status)) {
      return status;
    }
    if (!callback) {
      continue;
    }
    auto previous = current;
    current = expr->expr();
    if (!expr->ComesFromAst()) {
      // This step was added during compilation (e.g. Int64ConstImpl).
      continue;
    }
    if (!current) {
      continue;
    }
    if (current->expr_kind_case() == Expr::kComprehensionExpr &&
        previous != &current->comprehension_expr().result()) {
      // Callback is called with kComprehensionExpr multiple times
      // by ComprehensionNextStep, ComprehensionCondStep and
      // ComprehensionFinish. We should produce a value in trace only after
      // the final case, i.e. after ComprehensionFinish.
      continue;
    }
    if (stack->size() == 0) {
      GOOGLE_LOG(ERROR) << "Stack is empty after a ExpressionStep.Evaluate. "
                    "Try to disable short-circuiting.";
      continue;
    }
    auto status2 = callback(current, stack->Peek(), arena);
    if (!util::IsOk(status2)) {
      return status2;
    }
  }

  size_t final_stack_size = stack->size();
  if (initial_stack_size + 1 != final_stack_size || final_stack_size == 0) {
    return util::MakeStatus(google::rpc::Code::INTERNAL,
                        "Stack error during evaluation");
  }
  CelValue value = stack->Peek();
  stack->Pop(1);
  return value;
}

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google
