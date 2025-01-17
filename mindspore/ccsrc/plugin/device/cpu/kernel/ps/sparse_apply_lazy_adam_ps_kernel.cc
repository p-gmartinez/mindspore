/**
 * Copyright 2020-2022 Huawei Technologies Co., Ltd
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

#include "plugin/device/cpu/kernel/ps/sparse_apply_lazy_adam_ps_kernel.h"
#include <memory>
#include "kernel/common_utils.h"
#include "plugin/device/cpu/hal/device/cpu_device_address.h"
#include "ps/util.h"

namespace mindspore {
namespace kernel {
namespace ps {
constexpr size_t kSparseApplyLazyAdamPSInputsSize = 11;

void SparseApplyLazyAdamPSKernelMod::InitKernel(
  const CNodePtr &cnode, const std::shared_ptr<std::vector<std::shared_ptr<ShapeVector>>> &shapes) {
  MS_EXCEPTION_IF_NULL(cnode);
  MS_EXCEPTION_IF_NULL(shapes);
  const std::vector<std::shared_ptr<ShapeVector>> &shape_vec = *shapes;
  if (shape_vec.size() < kSparseApplyLazyAdamPSInputsSize) {
    MS_LOG(EXCEPTION) << "SparseApplyLazyAdamPSKernelMod needs " << kSparseApplyLazyAdamPSInputsSize
                      << " input shapes, but got " << shape_vec.size();
  }
  ShapeVector &var_shape = *(shape_vec[var_index_]);
  ShapeVector &m_shape = *(shape_vec[m_index_]);
  ShapeVector &v_shape = *(shape_vec[v_index_]);
  const ShapeVector &grad_shape = *(shape_vec[grad_index_]);
  const ShapeVector &indices_shape = *(shape_vec[indices_index_]);

  Shard(&var_shape, 0);
  Shard(&m_shape, 0);
  Shard(&v_shape, 0);

  if (var_shape.empty()) {
    MS_LOG(EXCEPTION) << "var must be at least 1D";
  }
  if (var_shape.size() != grad_shape.size()) {
    MS_LOG(EXCEPTION) << "var and grad must have the same shape size";
  }
  if (!IsSameShape(var_shape, m_shape)) {
    MS_LOG(EXCEPTION) << "var and m must have the same shape";
  }
  if (!IsSameShape(var_shape, v_shape)) {
    MS_LOG(EXCEPTION) << "var and v must have the same shape";
  }
  var_first_dim_size_ = LongToSize(var_shape[0]);
  for (size_t i = 1; i < var_shape.size(); ++i) {
    if (var_shape[i] != grad_shape[i]) {
      MS_LOG(EXCEPTION) << "The shape of var and grad must be equal in dimension " << i;
    }
    var_outer_dim_size_ *= LongToSize(var_shape[i]);
  }
  if (indices_shape.size() != 1) {
    MS_LOG(EXCEPTION) << "indices must be 1D";
  }
  indices_size_ = LongToSize(indices_shape[0]);
  if (grad_shape[0] != SizeToLong(indices_size_)) {
    MS_LOG(ERROR) << "The first dimension of grad shape must be equal to indices";
  }
  if (common::AnfAlgo::HasNodeAttr(USE_NESTEROV, cnode)) {
    use_nesterov_ = common::AnfAlgo::GetNodeAttr<bool>(cnode, USE_NESTEROV);
  }
  (void)workspace_size_list_.emplace_back(indices_size_ * var_outer_dim_size_ * sizeof(float) * worker_num_);
  (void)workspace_size_list_.emplace_back(indices_size_ * sizeof(int) * worker_num_);
  (void)workspace_size_list_.emplace_back(indices_size_ * var_outer_dim_size_ * sizeof(float) * worker_num_);
  (void)workspace_size_list_.emplace_back(indices_size_ * sizeof(int) * worker_num_);
}

void SparseApplyLazyAdamPSKernelMod::ReInit(const std::vector<ShapeVector> &shapes) {
  if (shapes.empty() || shapes[0].empty()) {
    MS_LOG(EXCEPTION) << "Shape can not empty";
  }
  const auto &indices_shape = shapes[0];
  indices_size_ = LongToSize(indices_shape[0]);
  workspace_size_list_[0] = indices_size_ * var_outer_dim_size_ * sizeof(float) * worker_num_;
  workspace_size_list_[1] = indices_size_ * sizeof(int) * worker_num_;
}

void SparseApplyLazyAdamPSKernelMod::ReInit(const std::vector<AddressPtr> &inputs) {
  if (inputs.size() < kSparseApplyLazyAdamPSInputsSize) {
    MS_LOG(EXCEPTION) << "Input shape size can not be less than " << kSparseApplyLazyAdamPSInputsSize << ", but got "
                      << inputs.size();
  }
  const auto &indices_addr = inputs[indices_index_];
  indices_size_ = indices_addr->size / sizeof(int);
  workspace_size_list_[0] = indices_size_ * var_outer_dim_size_ * sizeof(float) * worker_num_;
  workspace_size_list_[1] = indices_size_ * sizeof(int) * worker_num_;
}

bool SparseApplyLazyAdamPSKernelMod::Execute(const std::vector<AddressPtr> &inputs,
                                             const std::vector<AddressPtr> &workspace,
                                             const std::vector<AddressPtr> &outputs) {
  ReInit(inputs);
  if (indices_size_ == 0) {
    return true;
  }
  return Launch(inputs, workspace, outputs);
}

const std::vector<size_t> &SparseApplyLazyAdamPSKernelMod::input_sizes() const { return GetInputSizeList(); }

const std::vector<size_t> &SparseApplyLazyAdamPSKernelMod::output_sizes() const { return GetOutputSizeList(); }

const std::vector<size_t> &SparseApplyLazyAdamPSKernelMod::workspace_sizes() const { return GetWorkspaceSizeList(); }
}  // namespace ps
}  // namespace kernel
}  // namespace mindspore
