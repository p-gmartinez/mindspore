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

#include "cxx_api/model/acl/acl_model_multi.h"
#include <vector>
#include <utility>
#include <map>
#include <string>
#include <algorithm>
#include <numeric>
#include <deque>
#include <functional>
#include "backend/session/session_basic.h"
#include "backend/session/session_factory.h"
#include "backend/optimizer/common/optimizer.h"
#include "backend/optimizer/ascend/enhancer/add_placeholder_for_dynamic_rnn.h"
#include "cxx_api/factory.h"
#include "vm/backend.h"
#include "vm/transform.h"
#include "acl/acl_rt.h"
#include "mindspore/core/load_mindir/infer_mindir.h"
#include "debug/trace.h"

namespace mindspore {
API_FACTORY_REG(ModelImpl, Ascend310, AclModelMulti);

namespace {
std::map<DataType, size_t> kDtypeMap = {
  {DataType::kNumberTypeBool, sizeof(bool)},       {DataType::kNumberTypeInt8, sizeof(int8_t)},
  {DataType::kNumberTypeInt16, sizeof(int16_t)},   {DataType::kNumberTypeInt32, sizeof(int32_t)},
  {DataType::kNumberTypeInt64, sizeof(int64_t)},   {DataType::kNumberTypeFloat16, sizeof(float16)},
  {DataType::kNumberTypeFloat32, sizeof(float)},   {DataType::kNumberTypeFloat64, sizeof(double)},
  {DataType::kNumberTypeUInt8, sizeof(uint8_t)},   {DataType::kNumberTypeUInt16, sizeof(uint16_t)},
  {DataType::kNumberTypeUInt32, sizeof(uint32_t)}, {DataType::kNumberTypeUInt64, sizeof(uint64_t)}};

class MSTensorRef : public BaseRef {
 public:
  static VectorRef Convert(const std::vector<MSTensor> &tensors) {
    VectorRef res;
    std::transform(tensors.begin(), tensors.end(), std::back_inserter(res),
                   [](const MSTensor &t) { return MSTensorRef(t); });
    return res;
  }

  static std::vector<MSTensor> Convert(const BaseRef &args) {
    std::vector<MSTensor> res;
    if (utils::isa<VectorRef>(args)) {
      VectorRef args_vec = utils::cast<VectorRef>(args);
      res = ConvertTuple(args_vec);
    } else if (utils::isa<MSTensorRef>(args)) {
      auto wrapper = utils::cast<MSTensorRef>(args);
      res.push_back(wrapper.ms_tensor_);
    } else {
      MS_LOG(EXCEPTION) << "Invalid BaseRef " << args.ToString() << " must be MSTensorRef or VectorRef{MSTensorRef...}";
    }

    return res;
  }

  MS_DECLARE_PARENT(MSTensorRef, BaseRef);
  explicit MSTensorRef(const MSTensor &tensor) : ms_tensor_(tensor) {}
  ~MSTensorRef() override = default;

  const MSTensor &GetTensor() const { return ms_tensor_; }
  std::shared_ptr<Base> copy() const override {
    MSTensor *tensor = ms_tensor_.Clone();
    auto res = std::make_shared<MSTensorRef>(static_cast<const MSTensor &>(*tensor));
    MSTensor::DestroyTensorPtr(tensor);
    return res;
  }

  uint32_t type() const override { return tid(); }
  std::string ToString() const override { return ms_tensor_.Name(); }
  bool operator==(const BaseRef &other) const override {
    if (!utils::isa<MSTensorRef>(other)) {
      return false;
    }
    auto other_ms_tensor = utils::cast<MSTensorRef>(other).ms_tensor_;
    auto this_ms_tensor = ms_tensor_;
    return (this_ms_tensor.Name() == other_ms_tensor.Name()) && (this_ms_tensor.Shape() == other_ms_tensor.Shape()) &&
           (this_ms_tensor.MutableData() == other_ms_tensor.MutableData()) &&
           (this_ms_tensor.DataSize() == other_ms_tensor.DataSize()) &&
           (this_ms_tensor.DataType() == other_ms_tensor.DataType());
  }

 private:
  static std::vector<MSTensor> ConvertTuple(const VectorRef &args) {
    std::vector<MSTensor> outs;
    for (size_t i = 0; i < args.size(); ++i) {
      const auto &item = args[i];
      if (utils::isa<VectorRef>(item)) {
        VectorRef args_vec = utils::cast<VectorRef>(args);
        auto ret = ConvertTuple(args_vec);
        outs.insert(outs.end(), ret.begin(), ret.end());
      } else if (utils::isa<MSTensorRef>(item)) {
        auto wrapper = utils::cast<MSTensorRef>(item);
        outs.push_back(wrapper.ms_tensor_);
      } else {
        MS_LOG(EXCEPTION) << "Invalid BaseRef " << args.ToString()
                          << " must be MSTensorRef or VectorRef{MSTensorRef...}";
      }
    }
    return outs;
  }

  MSTensor ms_tensor_;
};

class MultiGraphAclSession : public session::SessionBasic {
 public:
  MultiGraphAclSession() = default;
  ~MultiGraphAclSession() override = default;
  void Init(uint32_t device_id) override;
  GraphId CompileGraphImpl(const AnfNodePtrList &lst, const AnfNodePtrList &outputs) override;
  void RunGraph(GraphId graph_id, const std::vector<MSTensor> &inputs, VectorRef *outputs);
  void SetOptions(const std::shared_ptr<AclModelOptions> &options) { options_ = options; }

 private:
  VectorRef ConstructOutputRef(GraphId graph_id, std::deque<MSTensor> *out_tensors);
  VectorRef ConstructOutputRefByTupleNode(const CNodePtr &tuple_node, std::deque<MSTensor> *out_tensors);

  std::map<GraphId, GraphCell> graphs_ = {};
  std::map<GraphId, KernelGraphPtr> kernel_graphs_ = {};
  std::shared_ptr<AclModelOptions> options_ = nullptr;
};

void MultiGraphAclSession::Init(uint32_t device_id) { InitExecutor(kDavinciMultiGraphInferenceDevice, device_id); }

GraphId MultiGraphAclSession::CompileGraphImpl(const AnfNodePtrList &lst, const AnfNodePtrList &outputs) {
  class FirstGraphModeGuard {
   public:
    explicit FirstGraphModeGuard(const std::shared_ptr<AclModelOptions> &options) : options_(options) {
      if (options_ != nullptr) {
        options_->SetFirstGraph(true);
      }
    }
    ~FirstGraphModeGuard() {
      if (options_ != nullptr) {
        options_->SetFirstGraph(false);
      }
    }

   private:
    std::shared_ptr<AclModelOptions> options_;
  };
  MS_LOG(INFO) << "Start MultiGraph Compile.";
  // construct kernel graph
  auto kernel_graph = SessionBasic::ConstructKernelGraph(lst, outputs, false);
  MS_EXCEPTION_IF_NULL(kernel_graph);
  auto optimizer = std::make_shared<opt::GraphOptimizer>();
  auto pm = std::make_shared<opt::PassManager>("310_multi_graph_pm");
  pm->AddPass(std::make_shared<opt::InsertPlaceholderForDynamicRNN>());
  optimizer->AddPassManager(pm);
  (void)optimizer->Optimize(kernel_graph);
  kernel_graph->SetExecOrderByDefault();
  // concert to om data
  ModelConverter model_converter_;
  model_converter_.set_options(options_);
  FirstGraphModeGuard guard(options_);
  auto om_data = model_converter_.LoadMindIR(kernel_graph);
  if (om_data.Data() == nullptr || om_data.DataSize() == 0) {
    MS_LOG(ERROR) << "Load MindIR failed.";
    return kMCFailed;
  }
  // load
  std::shared_ptr<Graph> graph = std::make_shared<Graph>(std::make_shared<Graph::GraphData>(om_data, ModelType::kOM));
  MS_EXCEPTION_IF_NULL(graph);
  auto graph_cell = GraphCell(graph);
  auto ret = graph_cell.Load(options_->GetDeviceID());
  if (ret != kSuccess) {
    MS_LOG(EXCEPTION) << "Load failed.";
  }
  graphs_[kernel_graph->graph_id()] = graph_cell;
  kernel_graphs_[kernel_graph->graph_id()] = kernel_graph;
  MS_LOG(INFO) << "Mulit graph compile success, graph id " << kernel_graph->graph_id();
  return kernel_graph->graph_id();
}

void MultiGraphAclSession::RunGraph(GraphId graph_id, const std::vector<MSTensor> &inputs, VectorRef *outputs) {
  MS_EXCEPTION_IF_NULL(outputs);
  MS_LOG(INFO) << "Start run graph " << graph_id;
  auto iter = graphs_.find(graph_id);
  if (iter == graphs_.end()) {
    MS_LOG(EXCEPTION) << "Graph id " << graph_id << " not found.";
  }
  std::vector<MSTensor> out_tensors;
  auto ret = iter->second.Run(inputs, &out_tensors);
  if (ret != kSuccess) {
    MS_LOG(EXCEPTION) << "Graph id " << graph_id << " run failed.";
  }

  std::deque<MSTensor> out_tensors_deque(out_tensors.begin(), out_tensors.end());
  (*outputs) = ConstructOutputRef(graph_id, &out_tensors_deque);
}

VectorRef MultiGraphAclSession::ConstructOutputRef(GraphId graph_id, std::deque<MSTensor> *out_tensors) {
  MS_EXCEPTION_IF_NULL(out_tensors);
  VectorRef outs;
  auto out_nodes = kernel_graphs_[graph_id]->outputs();
  for (auto &out : out_nodes) {
    if (out_tensors->empty()) {
      MS_LOG(EXCEPTION) << "Can not find MSTensor for output node " << out->DebugString();
    }
    auto item_with_index = AnfAlgo::VisitKernelWithReturnType(out, 0);
    auto &anf_node = item_with_index.first;
    if (AnfAlgo::CheckPrimitiveType(anf_node, prim::kPrimMakeTuple)) {
      auto cnode = anf_node->cast<CNodePtr>();
      MS_EXCEPTION_IF_NULL(cnode);
      outs.emplace_back(ConstructOutputRefByTupleNode(cnode, out_tensors));
    } else {
      outs.emplace_back(MSTensorRef(out_tensors->front()));
      out_tensors->pop_front();
    }
  }

  if (!out_tensors->empty()) {
    MS_LOG(EXCEPTION) << "Number of output size " << outs.size() << " but " << out_tensors->size()
                      << " MSTensor remained.";
  }

  return outs;
}

VectorRef MultiGraphAclSession::ConstructOutputRefByTupleNode(const CNodePtr &tuple_node,
                                                              std::deque<MSTensor> *out_tensors) {
  MS_EXCEPTION_IF_NULL(out_tensors);
  VectorRef outs;
  for (size_t i = 1; i < tuple_node->inputs().size(); ++i) {
    auto item_with_index = AnfAlgo::VisitKernelWithReturnType(tuple_node->input(i), 0);
    auto &anf_node = item_with_index.first;
    if (out_tensors->empty()) {
      MS_LOG(EXCEPTION) << "Can not find MSTensor for output node " << anf_node->DebugString();
    }

    if (AnfAlgo::CheckPrimitiveType(anf_node, prim::kPrimMakeTuple)) {
      auto cnode = anf_node->cast<CNodePtr>();
      MS_EXCEPTION_IF_NULL(cnode);
      outs.emplace_back(ConstructOutputRefByTupleNode(cnode, out_tensors));
    } else {
      outs.emplace_back(MSTensorRef(out_tensors->front()));
      out_tensors->pop_front();
    }
  }

  return outs;
}

class AclBackend : public compile::MsBackend {
 public:
  AclBackend(const std::string &name, const std::string &target, const std::shared_ptr<AclModelOptions> &options)
      : MsBackend(name, target, options->GetDeviceID()) {
    auto session = std::dynamic_pointer_cast<MultiGraphAclSession>(MsBackend::target_sess_);
    MS_EXCEPTION_IF_NULL(session);
    session->SetOptions(options);
  }

  ~AclBackend() override = default;

  VectorRef MsRunGraph(const GraphId &g, const VectorRef &args, const std::string &target) override {
    std::vector<MSTensor> inputs;
    for (const auto &arg : args) {
      if (!utils::isa<MSTensorRef>(arg)) {
        MS_LOG(EXCEPTION) << "Invalid item " << arg.ToString();
      }
      auto wrapper = utils::cast<MSTensorRef>(arg);
      inputs.emplace_back(wrapper.GetTensor());
    }

    VectorRef outputs;
    MS_EXCEPTION_IF_NULL(target_sess_);
    auto exec_sess = std::dynamic_pointer_cast<MultiGraphAclSession>(target_sess_);
    MS_EXCEPTION_IF_NULL(exec_sess);
    exec_sess->RunGraph(g, inputs, &outputs);
    return outputs;
  }

  bool GetCond(const BaseRef &c, bool *value) override {
    MS_EXCEPTION_IF_NULL(value);
    if (!utils::isa<MSTensorRef>(c)) {
      MS_LOG(ERROR) << "Invalid item " << c.ToString() << " must be a MSTensorRef.";
      return false;
    }
    auto wrapper = utils::cast<MSTensorRef>(c);
    if (wrapper.GetTensor().DataType() != DataType::kNumberTypeBool) {
      MS_LOG(ERROR) << "Invalid data type " << wrapper.GetTensor().DataType() << " must be bool.";
      return false;
    }
    auto data = wrapper.GetTensor().Data();
    if (data == nullptr) {
      return false;
    }
    (*value) = *reinterpret_cast<const bool *>(data.get());
    return true;
  }

  bool GetIndex(const BaseRef &c, int64_t *value) override {
    MS_EXCEPTION_IF_NULL(value);
    if (!utils::isa<MSTensorRef>(c)) {
      MS_LOG(ERROR) << "Invalid item " << c.ToString() << " must be a MSTensorRef.";
      return false;
    }

    auto wrapper = utils::cast<MSTensorRef>(c);
    if (wrapper.GetTensor().DataType() == DataType::kNumberTypeInt32) {
      auto data = wrapper.GetTensor().Data();
      if (data == nullptr) {
        return false;
      }
      auto value_int32 = *reinterpret_cast<const int32_t *>(data.get());
      (*value) = static_cast<int64_t>(value_int32);
      return true;
    } else if (wrapper.GetTensor().DataType() == DataType::kNumberTypeInt64) {
      auto data = wrapper.GetTensor().Data();
      if (data == nullptr) {
        return false;
      }
      (*value) = *reinterpret_cast<const int64_t *>(data.get());
      return true;
    } else {
      MS_LOG(ERROR) << "Index must be Int type.";
      return false;
    }
  }
};

class AclCompileGraph : public compile::CompileGraph {
 public:
  explicit AclCompileGraph(const std::shared_ptr<compile::MsBackend> &backend,
                           const std::vector<PrimitivePtr> &cut_list)
      : CompileGraph(backend, cut_list) {}
  ~AclCompileGraph() override = default;

  void AddInst(const compile::Instruction &inst, const MSTensorRef &arg) {
    VectorRef args;
    args.push_back(arg);
    compile::CompileGraph::AddInst(inst, args);
  }

  int64_t Ref(const AnfNodePtr &node) override {
    MS_EXCEPTION_IF_NULL(node);
    MS_LOG(DEBUG) << "Start Ref node " << node->DebugString(true) << " height_: " << height_;
    if (slots_.count(node) == 0 && node->isa<ValueNode>()) {
      if (IsValueNode<FuncGraph>(node)) {
        MS_LOG(DEBUG) << "Push graph.";
        compile::CompileGraph::AddInst(compile::Instruction::kGraph, GetValueNode(node));
      } else {
        MS_LOG(DEBUG) << "Push.";
        if (IsValueNode<Primitive>(node)) {
          MS_LOG(EXCEPTION) << "must not be primitive in here NodeInfo: " << trace::GetDebugInfo(node->debug_info());
        } else if (IsValueNode<tensor::Tensor>(node)) {
          auto tensor_node = std::dynamic_pointer_cast<tensor::Tensor>(node->cast<ValueNodePtr>()->value());
          MS_EXCEPTION_IF_NULL(tensor_node);
          std::string name = "";
          std::vector<int64_t> shape = tensor_node->shape_c();
          DataType type = static_cast<DataType>(tensor_node->data_type_c());
          auto mstensor_node = MSTensor::CreateRefTensor(name, type, shape, tensor_node->data_c(), tensor_node->Size());
          MSTensorRef mstensor_ref(*mstensor_node);
          AddInst(compile::Instruction::kPush, mstensor_ref);
          MSTensor::DestroyTensorPtr(mstensor_node);
        } else {
          compile::CompileGraph::AddInst(compile::Instruction::kPush, GetValueNode(node));
        }
      }
      Push(node);
    }
    MS_LOG(DEBUG) << "End Ref node end height_: " << height_ << ", slots: " << slots_[node]
                  << ", return: " << slots_[node] - height_;
    return slots_[node] - height_;
  }
};

class AclCompileGraphs : public compile::CompileGraphs {
 public:
  explicit AclCompileGraphs(const std::shared_ptr<compile::MsBackend> &backend,
                            const std::vector<PrimitivePtr> &cut_list)
      : CompileGraphs(backend, cut_list) {
    MS_EXCEPTION_IF_NULL(backend);
    MS_LOG(DEBUG) << "Start vm: " << backend->name();
    transform_ = std::make_shared<AclCompileGraph>(backend, cut_list);
    Reset();
  }
  ~AclCompileGraphs() override = default;
  void Compile(const FuncGraphPtr &graph) override {
    MS_LOG(DEBUG) << "Start";
    mapping_[graph] = SizeToLong(insts_.size());
    if (transform_ != nullptr) {
      auto insts = transform_->Run(graph, false);
      if (!insts.empty()) {
        (void)insts_.insert(insts_.end(), insts.begin(), insts.end());
      }
    }
    MS_LOG(DEBUG) << "End";
  }
};

std::shared_ptr<compile::MsBackend> CreateBackend(const std::shared_ptr<AclModelOptions> &options) {
  MS_EXCEPTION_IF_NULL(options);
  return std::make_shared<AclBackend>(kMsConvert, kDavinciMultiGraphInferenceDevice, options);
}

bool HasMultiGraph(const FuncGraphPtr &fg) {
  MS_EXCEPTION_IF_NULL(fg);
  std::vector<AnfNodePtr> all_nodes = TopoSort(fg->get_return());
  for (const auto &node : all_nodes) {
    MS_EXCEPTION_IF_NULL(node);
    if (IsValueNode<FuncGraph>(node)) {
      MS_LOG(INFO) << fg->ToString() << " has FuncGraph node " << node->DebugString() << " is multi graph.";
      return true;
    }
  }
  return false;
}
}  // namespace
Status AclModelMulti::Build() {
  if (!is_multi_graph_.has_value()) {
    is_multi_graph_ = ModelImpl::GetFuncGraph() == nullptr ? false : HasMultiGraph(ModelImpl::GetFuncGraph());
  }

  if (!is_multi_graph_.value()) {
    return AclModel::Build();
  }

  if (vm_ != nullptr) {
    MS_LOG(INFO) << "Multi graph model has been built, skip.";
    return kSuccess;
  }
  MS_LOG(INFO) << "Start build multi graph model.";
  // perpare func graph
  auto manager = MakeManager();
  manager->AddFuncGraph(ModelImpl::GetFuncGraph());
  ModelImpl::GetFuncGraph()->set_manager(manager);
  // set inputs
  SetInputs();
  // infer mindir
  abstract::AbstractBasePtrList broaded_args;
  auto fg = ModelImpl::GetFuncGraph();
  MS_EXCEPTION_IF_NULL(fg);
  const auto &inputs = fg->get_inputs();
  (void)std::transform(inputs.begin(), inputs.end(), std::back_inserter(broaded_args),
                       [](const AnfNodePtr &n) -> AbstractBasePtr {
                         MS_EXCEPTION_IF_NULL(n);
                         auto abstract = n->abstract();
                         MS_EXCEPTION_IF_NULL(abstract);
                         if (abstract->GetValueTrack() != kAnyValue) {
                           return abstract->Broaden();
                         }
                         return abstract;
                       });
  (void)InferMindir(ModelImpl::GetFuncGraph(), broaded_args);
  // set output
  SetOutput();
  // create vm
  auto backend = CreateBackend(std::make_shared<AclModelOptions>(model_context_));
  auto context_ptr = MsContext::GetInstance();
  MS_EXCEPTION_IF_NULL(context_ptr);
  backend->set_is_multi_graph_sink(false);
  context_ptr->set_param<std::string>(MS_CTX_DEVICE_TARGET, kDavinciMultiGraphInferenceDevice);
  context_ptr->set_param<bool>(MS_CTX_IS_MULTI_GRAPH_SINK, false);
  context_ptr->set_param<bool>(MS_CTX_ENABLE_LOOP_SINK, false);
  auto compile = std::make_shared<AclCompileGraphs>(backend, compile::GetMsNonlinearOps());

  vm_ = compile->CompileAndLink(ModelImpl::GetFuncGraph());
  backend_ = std::move(backend);
  MS_LOG(INFO) << "Build multi graph model success.";
  return kSuccess;
}

Status AclModelMulti::Predict(const std::vector<MSTensor> &inputs, std::vector<MSTensor> *outputs) {
  if (!is_multi_graph_.has_value()) {
    is_multi_graph_ = ModelImpl::GetFuncGraph() == nullptr ? false : HasMultiGraph(ModelImpl::GetFuncGraph());
  }

  if (!is_multi_graph_.value()) {
    return AclModel::Predict(inputs, outputs);
  }

  Build();
  MS_LOG(INFO) << "Start predict multi graph model.";
  MS_EXCEPTION_IF_NULL(vm_);
  MS_EXCEPTION_IF_NULL(outputs);
  try {
    (*outputs) = MSTensorRef::Convert(vm_->Eval(MSTensorRef::Convert(inputs)));
  } catch (const std::exception &ex) {
    MS_LOG(ERROR) << "Predict Failed, error: " << ex.what();
    return kMCFailed;
  }

  if (inputs_.empty()) {
    inputs_ = inputs;
  } else {
    if (inputs.size() != inputs_.size()) {
      MS_LOG(ERROR) << "Input Size is wrong.";
      return kMCFailed;
    }
    for (size_t i = 0; i < inputs_.size(); ++i) {
      auto input_tensor = MSTensor::CreateTensor(inputs_[i].Name(), inputs_[i].DataType(), inputs_[i].Shape(),
                                                 inputs[i].Data().get(), inputs[i].DataSize());
      inputs_[i] = (*input_tensor);
      MSTensor::DestroyTensorPtr(input_tensor);
    }
  }

  outputs_ = *outputs;
  MS_LOG(INFO) << "Predict multi graph model success.";
  return kSuccess;
}

void AclModelMulti::SetInputs() {
  if (inputs_.empty()) {
    auto fg = ModelImpl::GetFuncGraph();
    MS_EXCEPTION_IF_NULL(fg);
    const auto &inputs = fg->get_inputs();
    for (const auto &in : inputs) {
      auto input_param = std::dynamic_pointer_cast<Parameter>(in);
      MS_EXCEPTION_IF_NULL(input_param);
      MS_EXCEPTION_IF_NULL(input_param->abstract());
      auto input_value = input_param->abstract()->GetValueTrack();
      auto tensor = input_value->cast<tensor::TensorPtr>();
      MS_EXCEPTION_IF_NULL(tensor);

      std::vector<int64_t> shape = tensor->shape_c();
      auto input_tensor = MSTensor::CreateTensor(input_param->name(), static_cast<DataType>(tensor->data_type_c()),
                                                 shape, nullptr, tensor->Size());
      inputs_.emplace_back(*input_tensor);
      MSTensor::DestroyTensorPtr(input_tensor);
    }
  } else {
    MS_LOG(DEBUG) << "inputs_ has been set.";
  }
}

void AclModelMulti::SetOutput() {
  if (outputs_.empty()) {
    auto fg = ModelImpl::GetFuncGraph();
    MS_EXCEPTION_IF_NULL(fg);
    const auto output = fg->output();
    MS_EXCEPTION_IF_NULL(output);
    auto abs = output->abstract();
    MS_EXCEPTION_IF_NULL(abs);

    // DataType
    DataType type_id;
    if (abs->isa<abstract::AbstractTensor>()) {
      auto abs_tensor = abs->cast<abstract::AbstractTensorPtr>();
      auto ele = abs_tensor->element();
      MS_EXCEPTION_IF_NULL(ele);
      MS_EXCEPTION_IF_NULL(ele->GetTypeTrack());
      type_id = static_cast<DataType>(ele->GetTypeTrack()->type_id());
    } else {
      MS_EXCEPTION_IF_NULL(abs->GetTypeTrack());
      type_id = static_cast<DataType>(abs->GetTypeTrack()->type_id());
    }
    // Shape
    auto shape_track = abs->GetShapeTrack();
    MS_EXCEPTION_IF_NULL(shape_track);
    std::vector<int64_t> shape = {};
    if (shape_track->isa<abstract::Shape>()) {
      auto shapeptr = shape_track->cast<abstract::ShapePtr>();
      shape = static_cast<std::vector<int64_t>>(shapeptr->shape());
    }
    // Size
    size_t ato_size = 0;
    if (kDtypeMap.find(type_id) != kDtypeMap.end()) {
      ato_size = kDtypeMap[type_id];
    }
    int64_t ele_num = std::accumulate(shape.begin(), shape.end(), 1, std::multiplies<int64_t>());
    size_t size = ato_size * ele_num;
    // create tensor
    auto output_tensor = MSTensor::CreateTensor("", type_id, shape, nullptr, size);
    outputs_.emplace_back(*output_tensor);
    MSTensor::DestroyTensorPtr(output_tensor);
  } else {
    MS_LOG(DEBUG) << "outputs_ has been set.";
  }
}

std::vector<MSTensor> AclModelMulti::GetInputs() {
  if (!is_multi_graph_.has_value()) {
    is_multi_graph_ = ModelImpl::GetFuncGraph() == nullptr ? false : HasMultiGraph(ModelImpl::GetFuncGraph());
  }

  if (!is_multi_graph_.value()) {
    return AclModel::GetInputs();
  }

  return inputs_;
}

std::vector<MSTensor> AclModelMulti::GetOutputs() {
  if (!is_multi_graph_.has_value()) {
    is_multi_graph_ = ModelImpl::GetFuncGraph() == nullptr ? false : HasMultiGraph(ModelImpl::GetFuncGraph());
  }

  if (!is_multi_graph_.value()) {
    return AclModel::GetOutputs();
  }

  return outputs_;
}

namespace session {
MS_REG_SESSION(kDavinciMultiGraphInferenceDevice, MultiGraphAclSession);
}  // namespace session
}  // namespace mindspore
