/**
 * Copyright 2021 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "runtime/graph_scheduler/actor/abstract_actor.h"
#include "runtime/graph_scheduler/actor/output_actor.h"
#include "utils/log_adapter.h"

namespace mindspore {
namespace runtime {
void AbstractActor::RunOpData(OpData<DeviceTensor> *const input_data, OpContext<DeviceTensor> *const context) {
  MS_EXCEPTION_IF_NULL(input_data);
  MS_EXCEPTION_IF_NULL(input_data->data_);
  MS_EXCEPTION_IF_NULL(input_data->data_->GetPtr());
  MS_EXCEPTION_IF_NULL(context);
  auto &sequential_num = context->sequential_num_;
  (void)input_op_datas_[sequential_num].emplace_back(input_data);

  auto is_run = CheckRunningCondition(context);
  MS_LOG(DEBUG) << "Actor(" << GetAID().Name() << ") receive the input op data and check running condition:" << is_run;
  if (is_run) {
    Run(context);
  }
}

void AbstractActor::RunOpControl(AID *const input_control, OpContext<DeviceTensor> *const context) {
  MS_EXCEPTION_IF_NULL(context);
  auto &sequential_num = context->sequential_num_;
  (void)input_op_controls_[sequential_num].emplace_back(input_control);

  auto is_run = CheckRunningCondition(context);
  MS_LOG(DEBUG) << "Actor(" << GetAID().Name()
                << ") receive the input op control and check running condition:" << is_run;
  if (is_run) {
    Run(context);
  }
}

void AbstractActor::RunBatchOpData(std::vector<OpData<DeviceTensor> *> *const batch_input_data,
                                   OpContext<DeviceTensor> *const context) {
  MS_EXCEPTION_IF_NULL(batch_input_data);
  MS_LOG(DEBUG) << "Actor(" << GetAID().Name() << ") receive the batch input op data.";
  for (auto &input_data : *batch_input_data) {
    RunOpData(input_data, context);
  }
}

bool AbstractActor::CheckRunningCondition(const OpContext<DeviceTensor> *context) const {
  MS_EXCEPTION_IF_NULL(context);
  if (input_datas_num_ != 0) {
    const auto &data_iter = input_op_datas_.find(context->sequential_num_);
    if (data_iter == input_op_datas_.end()) {
      return false;
    }
    if (data_iter->second.size() < input_datas_num_) {
      return false;
    } else if (data_iter->second.size() > input_datas_num_) {
      MS_LOG(ERROR) << "Invalid input data num:" << data_iter->second.size() << " need:" << input_datas_num_
                    << " for actor:" << GetAID();
      return false;
    }
  }

  if (input_controls_num_ != 0) {
    const auto &control_iter = input_op_controls_.find(context->sequential_num_);
    if (control_iter == input_op_controls_.end()) {
      return false;
    }
    if (control_iter->second.size() < input_controls_num_) {
      return false;
    } else if (control_iter->second.size() > input_controls_num_) {
      MS_LOG(ERROR) << "Invalid input control num:" << control_iter->second.size() << " need:" << input_controls_num_
                    << " for actor:" << GetAID();
      return false;
    }
  }
  return true;
}

void AbstractActor::EraseInput(const OpContext<DeviceTensor> *context) {
  MS_EXCEPTION_IF_NULL(context);
  if ((input_datas_num_ != 0) && (!input_op_datas_.empty())) {
    auto ret = input_op_datas_.erase(context->sequential_num_);
    if (ret == 0) {
      std::string error_info = "Erase input data failed: " + GetAID().Name();
      // The sequential num may be invalid, can't set the promise value of context.
      MS_LOG(ERROR) << error_info << ", sequential_num: " << context->sequential_num_;
      return;
    }
  }

  if ((input_controls_num_ != 0) && (!input_op_controls_.empty())) {
    auto ret = input_op_controls_.erase(context->sequential_num_);
    if (ret == 0) {
      std::string error_info = "Erase input controls failed: " + GetAID().Name();
      // The sequential num may be invalid, can't set the promise value of context.
      MS_LOG(ERROR) << error_info << ", sequential_num: " << context->sequential_num_;
      return;
    }
  }
}

void AbstractActor::InitOutputData() {
  mindspore::HashMap<std::string, size_t> batch_op_count;
  for (auto &data_arrow : output_data_arrows_) {
    MS_EXCEPTION_IF_NULL(data_arrow);
    auto data = std::make_unique<OpData<DeviceTensor>>(data_arrow->to_op_id_, nullptr, data_arrow->to_input_index_);
    auto &to_op_name = data_arrow->to_op_id_.Name();

    // Identify whether the output data flag is kOutputDataFalgToStack.
    bool is_to_stack = (to_op_name.find(kStackActorNameSuffix) != std::string::npos);
    int output_data_flag = (is_to_stack == true) ? kOutputDataFalgToStack : kOutputDataFalgInit;

    // Add the batch output data.
    if (data_arrow->flag_ == kOutputDataFalgBatch) {
      if (is_to_stack) {
        MS_LOG(EXCEPTION) << "Not support the batch output data to stack actor.";
      }
      (void)batch_output_data_[to_op_name].emplace_back(data.get());

      output_data_flag = kOutputDataFalgBatch;
      // Identify whether the output data flag is kOutputDataFalgLastBatch.
      ++(batch_op_count[to_op_name]);
      if (batch_op_count[to_op_name] == batch_output_data_arrows_[to_op_name].size()) {
        output_data_flag = kOutputDataFalgLastBatch;
      }
    }

    // Add the output data.
    (void)output_data_.emplace_back(std::make_pair(std::move(data), output_data_flag));
  }
}

void AbstractActor::SendOutput(OpContext<DeviceTensor> *const context) {
  MS_EXCEPTION_IF_NULL(context);
  // Must be the execution order: send data --> send control, avoid the illegal timing problem.
  // 1.Send output data.
  if (((output_data_arrows_.size() != output_data_.size()) ||
       (output_data_arrows_.size() != output_data_nodes_.size())) &&
      (type_ < KernelTransformType::kSwitchActor)) {
    SET_OPCONTEXT_FAIL_RET_WITH_ERROR((*context), "The size of output data arrows is not equal to the output data.");
  }

  size_t output_data_arrow_index = 0;
  for (auto &output_data : output_data_) {
    MS_EXCEPTION_IF_NULL(output_data.first);
    MS_EXCEPTION_IF_NULL(output_data_arrows_[output_data_arrow_index]);
    auto &to_op_id = output_data.first->op_id_;
    UpdateOutputData(output_data.first.get(), output_data_arrows_[output_data_arrow_index],
                     output_data_nodes_[output_data_arrow_index], context);

    if (output_data.second == kOutputDataFalgLastBatch) {
      // Send batch output data. As the data need update, so all data must be collected completely before sending.
      ActorDispatcher::Send(to_op_id, &AbstractActor::RunBatchOpData, &batch_output_data_[to_op_id.Name()], context);
    } else if (output_data.second == kOutputDataFalgToStack) {
      // Create a new op data for stack actor.
      auto to_stack_data =
        std::make_unique<OpData<DeviceTensor>>(to_op_id, output_data.first->data_, output_data.first->index_);
      (void)to_stack_data_.emplace_back(std::move(to_stack_data));
      ActorDispatcher::Send(to_op_id, &OpActor::RunOpData, to_stack_data_.back().get(), context);
    } else if (output_data.second != kOutputDataFalgBatch) {
      // The batch output data only send when the output flag is kOutputDataFalgLastBatch.
      ActorDispatcher::Send(to_op_id, &OpActor::RunOpData, output_data.first.get(), context);
    }
    ++output_data_arrow_index;
  }

  // 2.Send output control.
  if (output_control_arrows_.size() > 0) {
    auto from_aid = const_cast<AID *>(&GetAID());
    for (auto &output_control : output_control_arrows_) {
      ActorDispatcher::Send(output_control, &OpActor::RunOpControl, from_aid, context);
    }
  }

  // 3.Send recorder info.
  SendRecorderInfo(context);

  // No output.
  if ((output_data_arrows_.size() == 0) && (output_control_arrows_.size() == 0) &&
      (type_ < KernelTransformType::kSwitchActor)) {
    SET_OPCONTEXT_SUCCESS_RET((*context));
  }
}
}  // namespace runtime
}  // namespace mindspore
