#include "eval/eval/create_list_step.h"
#include "eval/eval/container_backed_list_impl.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

namespace {

class CreateListStep : public ExpressionStepBase {
 public:
  CreateListStep(const google::api::expr::v1alpha1::Expr* expr, int list_size)
      : ExpressionStepBase(expr), list_size_(list_size) {}

  util::Status Evaluate(ExecutionFrame* frame) const override;

 private:
  int list_size_;
};

util::Status CreateListStep::Evaluate(ExecutionFrame* frame) const {
  if (list_size_ < 0) {
    return util::MakeStatus(google::rpc::Code::INTERNAL,
                        "CreateListStep: list size is <0");
  }

  if (!frame->value_stack().HasEnough(list_size_)) {
    return util::MakeStatus(google::rpc::Code::INTERNAL,
                        "CreateListStep: stack undeflow");
  }

  auto args = frame->value_stack().GetSpan(list_size_);

  CelList* cel_list = google::protobuf::Arena::Create<ContainerBackedListImpl>(
      frame->arena(), std::vector<CelValue>(args.begin(), args.end()));

  frame->value_stack().Pop(list_size_);
  frame->value_stack().Push(CelValue::CreateList(cel_list));

  return util::OkStatus();
}

}  // namespace

// Factory method for CreateList - based Execution step
util::StatusOr<std::unique_ptr<ExpressionStep>> CreateCreateListStep(
    const google::api::expr::v1alpha1::Expr::CreateList* create_list_expr,
    const google::api::expr::v1alpha1::Expr* expr) {
  std::unique_ptr<ExpressionStep> step = absl::make_unique<CreateListStep>(
      expr, create_list_expr->elements_size());
  return std::move(step);
}

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google
