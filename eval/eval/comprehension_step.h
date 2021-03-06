#ifndef THIRD_PARTY_CEL_CPP_EVAL_EVAL_COMPREHENSION_STEP_H_
#define THIRD_PARTY_CEL_CPP_EVAL_EVAL_COMPREHENSION_STEP_H_

#include "eval/eval/evaluator_core.h"
#include "eval/eval/expression_step_base.h"
#include "eval/public/activation.h"
#include "eval/public/cel_function.h"
#include "eval/public/cel_value.h"
#include "google/api/expr/v1alpha1/syntax.pb.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

class ComprehensionNextStep : public ExpressionStepBase {
 public:
  ComprehensionNextStep(const std::string& accu_var, const std::string& iter_var,
                        const google::api::expr::v1alpha1::Expr* expr);

  void set_jump_offset(int offset);
  void set_error_jump_offset(int offset);

  util::Status Evaluate(ExecutionFrame* frame) const override;

 private:
  std::string accu_var_;
  std::string iter_var_;
  int jump_offset_;
  int error_jump_offset_;
};

class ComprehensionCondStep : public ExpressionStepBase {
 public:
  ComprehensionCondStep(const std::string& accu_var, const std::string& iter_var,
                        bool shortcircuiting,
                        const google::api::expr::v1alpha1::Expr* expr);

  void set_jump_offset(int offset);

  util::Status Evaluate(ExecutionFrame* frame) const override;

 private:
  std::string iter_var_;
  int jump_offset_;
  bool shortcircuiting_;
};

class ComprehensionFinish : public ExpressionStepBase {
 public:
  ComprehensionFinish(const std::string& accu_var, const std::string& iter_var,
                      const google::api::expr::v1alpha1::Expr* expr);

  util::Status Evaluate(ExecutionFrame* frame) const override;

 private:
  std::string accu_var_;
};

// Creates a step that lists the map keys if the top of the stack is a map,
// otherwise it's a no-op.
std::unique_ptr<ExpressionStep> CreateListKeysStep(
    const google::api::expr::v1alpha1::Expr* expr);

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google

#endif  // THIRD_PARTY_CEL_CPP_EVAL_EVAL_COMPREHENSION_STEP_H_
