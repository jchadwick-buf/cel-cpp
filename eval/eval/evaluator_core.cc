#include "eval/eval/evaluator_core.h"

#include <memory>
#include <set>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "base/type_provider.h"
#include "base/value_factory.h"
#include "eval/eval/attribute_trail.h"
#include "eval/internal/interop.h"
#include "eval/public/cel_value.h"
#include "extensions/protobuf/memory_manager.h"
#include "internal/casts.h"

namespace google::api::expr::runtime {

namespace {

absl::Status InvalidIterationStateError() {
  return absl::InternalError(
      "Attempted to access iteration variable outside of comprehension.");
}

}  // namespace

// TODO(issues/5): cel::TypeFactory and family are setup here assuming legacy
// value interop. Later, these will need to be configurable by clients.
CelExpressionFlatEvaluationState::CelExpressionFlatEvaluationState(
    size_t value_stack_size, google::protobuf::Arena* arena)
    : memory_manager_(arena),
      value_stack_(value_stack_size),
      type_factory_(memory_manager_),
      type_manager_(type_factory_, cel::TypeProvider::Builtin()),
      value_factory_(type_manager_) {}

void CelExpressionFlatEvaluationState::Reset() {
  iter_stack_.clear();
  value_stack_.Clear();
}

const ExpressionStep* ExecutionFrame::Next() {
  size_t end_pos = execution_path_.size();

  if (pc_ < end_pos) return execution_path_[pc_++].get();
  if (pc_ > end_pos) {
    LOG(ERROR) << "Attempting to step beyond the end of execution path.";
  }
  return nullptr;
}

absl::Status ExecutionFrame::PushIterFrame(absl::string_view iter_var_name,
                                           absl::string_view accu_var_name) {
  CelExpressionFlatEvaluationState::IterFrame frame;
  frame.iter_var = {iter_var_name, cel::Handle<cel::Value>(), AttributeTrail()};
  frame.accu_var = {accu_var_name, cel::Handle<cel::Value>(), AttributeTrail()};
  state_->iter_stack().push_back(std::move(frame));
  return absl::OkStatus();
}

absl::Status ExecutionFrame::PopIterFrame() {
  if (state_->iter_stack().empty()) {
    return absl::InternalError("Loop stack underflow.");
  }
  state_->iter_stack().pop_back();
  return absl::OkStatus();
}

absl::Status ExecutionFrame::SetAccuVar(cel::Handle<cel::Value> value) {
  return SetAccuVar(std::move(value), AttributeTrail());
}

absl::Status ExecutionFrame::SetAccuVar(cel::Handle<cel::Value> value,
                                        AttributeTrail trail) {
  if (state_->iter_stack().empty()) {
    return InvalidIterationStateError();
  }
  auto& iter = state_->IterStackTop();
  iter.accu_var.value = std::move(value);
  iter.accu_var.attr_trail = std::move(trail);
  return absl::OkStatus();
}

absl::Status ExecutionFrame::SetIterVar(cel::Handle<cel::Value> value,
                                        AttributeTrail trail) {
  if (state_->iter_stack().empty()) {
    return InvalidIterationStateError();
  }
  auto& iter = state_->IterStackTop();
  iter.iter_var.value = std::move(value);
  iter.iter_var.attr_trail = std::move(trail);
  return absl::OkStatus();
}

absl::Status ExecutionFrame::SetIterVar(cel::Handle<cel::Value> value) {
  return SetIterVar(std::move(value), AttributeTrail());
}

absl::Status ExecutionFrame::ClearIterVar() {
  if (state_->iter_stack().empty()) {
    return InvalidIterationStateError();
  }
  state_->IterStackTop().iter_var.value = cel::Handle<cel::Value>();
  return absl::OkStatus();
}

bool ExecutionFrame::GetIterVar(absl::string_view name,
                                cel::Handle<cel::Value>* value,
                                AttributeTrail* trail) const {
  for (auto iter = state_->iter_stack().rbegin();
       iter != state_->iter_stack().rend(); ++iter) {
    auto& frame = *iter;
    if (frame.iter_var.value && name == frame.iter_var.name) {
      if (value != nullptr) {
        *value = frame.iter_var.value;
      }
      if (trail != nullptr) {
        *trail = frame.iter_var.attr_trail;
      }
      return true;
    }
    if (frame.accu_var.value && name == frame.accu_var.name) {
      if (value != nullptr) {
        *value = frame.accu_var.value;
      }
      if (trail != nullptr) {
        *trail = frame.accu_var.attr_trail;
      }
      return true;
    }
  }

  return false;
}

std::unique_ptr<CelEvaluationState> CelExpressionFlatImpl::InitializeState(
    google::protobuf::Arena* arena) const {
  return std::make_unique<CelExpressionFlatEvaluationState>(path_.size(),
                                                            arena);
}

absl::StatusOr<CelValue> CelExpressionFlatImpl::Evaluate(
    const BaseActivation& activation, CelEvaluationState* state) const {
  return Trace(activation, state, CelEvaluationListener());
}

absl::StatusOr<CelValue> CelExpressionFlatImpl::Trace(
    const BaseActivation& activation, CelEvaluationState* _state,
    CelEvaluationListener callback) const {
  auto state =
      ::cel::internal::down_cast<CelExpressionFlatEvaluationState*>(_state);
  state->Reset();

  ExecutionFrame frame(path_, activation, &type_registry_, options_, state);

  EvaluatorStack* stack = &frame.value_stack();
  size_t initial_stack_size = stack->size();
  const ExpressionStep* expr;
  while ((expr = frame.Next()) != nullptr) {
    auto status = expr->Evaluate(&frame);
    if (!status.ok()) {
      return status;
    }
    if (!callback) {
      continue;
    }
    if (!expr->ComesFromAst()) {
      // This step was added during compilation (e.g. Int64ConstImpl).
      continue;
    }

    if (stack->empty()) {
      LOG(ERROR) << "Stack is empty after a ExpressionStep.Evaluate. "
                    "Try to disable short-circuiting.";
      continue;
    }
    auto status2 =
        callback(expr->id(),
                 cel::interop_internal::ModernValueToLegacyValueOrDie(
                     state->arena(), stack->Peek()),
                 state->arena());
    if (!status2.ok()) {
      return status2;
    }
  }

  size_t final_stack_size = stack->size();
  if (initial_stack_size + 1 != final_stack_size || final_stack_size == 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Stack error during evaluation");
  }
  auto value = stack->Peek();
  stack->Pop(1);
  return cel::interop_internal::ModernValueToLegacyValueOrDie(state->arena(),
                                                              value);
}

}  // namespace google::api::expr::runtime
