/**
 * Copyright 2020 Huawei Technologies Co., Ltd
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

#ifndef DATASET_KERNELS_IMAGE_BOUNDING_BOX_AUGMENT_OP_H_
#define DATASET_KERNELS_IMAGE_BOUNDING_BOX_AUGMENT_OP_H_

#include <memory>
#include <random>
#include <cstdlib>
#include <opencv2/imgproc/imgproc.hpp>
#include "dataset/core/tensor.h"
#include "dataset/kernels/tensor_op.h"
#include "dataset/util/status.h"
#include "dataset/util/random.h"

namespace mindspore {
namespace dataset {
class BoundingBoxAugmentOp : public TensorOp {
 public:
  // Default values, also used by python_bindings.cc
  static const float kDefRatio;

  // Constructor for BoundingBoxAugmentOp
  // @param std::shared_ptr<TensorOp> transform transform: C++ opration to apply on select bounding boxes
  // @param float ratio: ratio of bounding boxes to have the transform applied on
  BoundingBoxAugmentOp(std::shared_ptr<TensorOp> transform, float ratio);

  ~BoundingBoxAugmentOp() override = default;

  // Provide stream operator for displaying it
  friend std::ostream &operator<<(std::ostream &out, const BoundingBoxAugmentOp &so) {
    so.Print(out);
    return out;
  }

  void Print(std::ostream &out) const override { out << "BoundingBoxAugmentOp"; }

  Status Compute(const TensorRow &input, TensorRow *output) override;

 private:
  float ratio_;
  std::mt19937 rnd_;
  std::shared_ptr<TensorOp> transform_;
};
}  // namespace dataset
}  // namespace mindspore

#endif  // DATASET_KERNELS_IMAGE_BOUNDING_BOX_AUGMENT_OP_H_
