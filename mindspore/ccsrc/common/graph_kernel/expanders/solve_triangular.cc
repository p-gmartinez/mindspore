/**
 * Copyright 2022 Huawei Technologies Co., Ltd
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
#include <string>
#include <utility>
#include <vector>
#include <memory>

#include "common/graph_kernel/expanders/op_desc_registry.h"
#include "common/graph_kernel/expanders/utils.h"

namespace mindspore::graphkernel::expanders {
constexpr int64_t kBlock = 16;
class SolveTriangular : public OpDesc {
 public:
  SolveTriangular() {}
  ~SolveTriangular() = default;

 protected:
  NodePtrList Expand(const NodePtrList &inputs) override {
    auto input_x = inputs[0];
    auto input_y = inputs[1];
    auto num = input_x->shape[0];
    auto loop_count = static_cast<int64_t>(num / kBlock);
    std::vector<int64_t> strides{1, 1};

    // solve_triangular dsl  implementation
    for (int64_t i = 0; i < loop_count; ++i) {
      std::vector<int64_t> begin_1{i * kBlock, i * kBlock};
      std::vector<int64_t> end_1{(i + 1) * kBlock, (i + 1) * kBlock};
      std::vector<int64_t> begin_2{i * kBlock, 0};
      std::vector<int64_t> end_2{(i + 1) * kBlock, kBlock};

      auto stride_x = gb.StridedSlice(input_x, begin_1, end_1, strides);
      auto stride_y = gb.StridedSlice(input_y, begin_2, end_2, strides);
      std::string func_type = "hybrid";
      std::string func_name = "trsmL";
      std::string func_source_str =
        "def trsmL(a, b):\n    inverse_0 = allocate(b.shape, b.dtype)\n    row = b.shape[0]\n    col = b.shape[1]\n    "
        "for l in range(col // 16):\n        for i in range(row):\n            for j in range(i):\n                for "
        "k in range(16):\n                    inverse_0[i, l * 16 + k] = a[i, j] * b[j, l * 16 + k]\n                  "
        "  b[i, l * 16 + k] = b[i, l * 16 + k] - inverse_0[i, l * 16 + k]\n            for k in range(16):\n           "
        "     b[i, l * 16 + k] = b[i, l * 16 + k] / a[i, i]\n\n    return b\n";
      size_t inplace_assign_output = 1;
      std::string func_compile_attrs = "{\"enable_mlsched\": true}";
      auto custom_result = gb.Custom({stride_x, stride_y}, {stride_y->shape, stride_y->type, stride_y->format},
                                     func_name, func_type, func_source_str, inplace_assign_output, func_compile_attrs);
      ShapeVector indices_shape{kBlock, kBlock, 2};
      std::vector<int64_t> indicse_value;
      for (int64_t u = i * kBlock; u < (i + 1) * kBlock; ++u) {
        for (int64_t v = 0; v < kBlock; ++v) {
          (void)indicse_value.emplace_back(u);
          (void)indicse_value.emplace_back(v);
        }
      }
      auto indices_tensor =
        std::make_shared<tensor::Tensor>(kNumberTypeInt64, indices_shape, &indicse_value[0], kNumberTypeInt64);
      auto indices = gb.Value(indices_tensor);
      input_y = gb.Emit("ScatterNdUpdate", {input_y, indices, custom_result}, {{"use_locking", MakeValue(false)}});

      if (i < loop_count - 1) {
        std::vector<int64_t> begin_3{(i + 1) * kBlock, 0};
        std::vector<int64_t> end_3{num, kBlock};
        std::vector<int64_t> begin_4{(i + 1) * kBlock, i * kBlock};
        std::vector<int64_t> end_4{num, (i + 1) * kBlock};
        std::vector<int64_t> begin_5{i * kBlock, 0};
        std::vector<int64_t> end_5{(i + 1) * kBlock, kBlock};
        auto stride_final_upd = gb.StridedSlice(input_y, begin_3, end_3, strides);
        auto stride_final_x = gb.StridedSlice(input_x, begin_4, end_4, strides);
        // on ascend, matmul's inputs must be fp16
        stride_final_x = gb.Cast(stride_final_x, kNumberTypeFloat16);
        auto stride_final_y = gb.StridedSlice(input_y, begin_5, end_5, strides);
        stride_final_y = gb.Cast(stride_final_y, kNumberTypeFloat16);
        auto matmul_final_x_y = gb.MatMul(stride_final_x, stride_final_y);
        matmul_final_x_y = gb.Cast(matmul_final_x_y, kNumberTypeFloat32);
        auto final_update_y = gb.Sub(stride_final_upd, matmul_final_x_y);
        std::vector<int64_t> final_indicse_value;
        for (int64_t u = (i + 1) * kBlock; u < num; ++u) {
          for (int64_t v = 0; v < kBlock; ++v) {
            (void)final_indicse_value.emplace_back(u);
            (void)final_indicse_value.emplace_back(v);
          }
        }
        auto final_indices_tensor =
          std::make_shared<tensor::Tensor>(kNumberTypeInt64, indices_shape, &final_indicse_value[0], kNumberTypeInt64);
        auto final_indices = gb.Value(final_indices_tensor);
        input_y =
          gb.Emit("ScatterNdUpdate", {input_y, final_indices, final_update_y}, {{"use_locking", MakeValue(false)}});
      }
    }
    return {input_y};
  }
};
EXPANDER_OP_DESC_REGISTER("SolveTriangular", SolveTriangular);
}  // namespace mindspore::graphkernel::expanders
