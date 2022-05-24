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
#include "common/graph_kernel/parallel_optimizer.h"

#include <vector>
#include <utility>
#include <algorithm>
#include "include/common/utils/utils.h"

namespace mindspore::graphkernel {
constexpr auto USER_NUM = 2;
constexpr auto REAL_NODE_START_POS = 2;
constexpr auto UMONAD_POS = 1;
namespace {
std::pair<AnfNodePtr, AnfNodePtr> IsTargetUpdateState(const AnfNodePtr &node, const PrimitivePtr &opt_prim,
                                                      const FuncGraphManagerPtr &mng) {
  auto users = mng->node_users()[node];
  if (users.size() != USER_NUM) return std::make_pair(nullptr, nullptr);
  if (IsPrimitiveCNode(users.front().first, prim::kPrimUpdateState) && IsPrimitiveCNode(users.back().first, opt_prim)) {
    return std::make_pair(users.front().first, users.back().first);
  }
  if (IsPrimitiveCNode(users.back().first, prim::kPrimUpdateState) && IsPrimitiveCNode(users.front().first, opt_prim)) {
    return std::make_pair(users.back().first, users.front().first);
  }
  return std::make_pair(nullptr, nullptr);
}
// Originally, multiple optimizers are kept in serial order by UpdateState
// If two optimizers are connected through a path started from any of optimizer's parameter input, then a serial order
// is necessary. Otherwise, parallel is reasonable
bool CanParallel(const mindspore::HashSet<AnfNodePtr> &opts_set, const FuncGraphManagerPtr &mng) {
  mindspore::HashMap<AnfNodePtr, mindspore::HashSet<AnfNodePtr>> other_nodes_to_opts;
  std::function<mindspore::HashSet<AnfNodePtr>(AnfNodePtr)> dfs;
  dfs = [&dfs, &other_nodes_to_opts, &opts_set, &mng](const AnfNodePtr &cur_node) {
    if (other_nodes_to_opts.count(cur_node) > 0) {
      return other_nodes_to_opts[cur_node];
    }
    auto users = mng->node_users()[cur_node];
    mindspore::HashSet<AnfNodePtr> tmp;
    for (auto &i : users) {
      if (opts_set.count(i.first) > 0) {
        (void)tmp.insert(i.first);
      } else {
        auto res = dfs(i.first);
        (void)tmp.insert(res.begin(), res.end());
      }
    }
    other_nodes_to_opts[cur_node] = tmp;
    return tmp;
  };

  for (auto &opt : opts_set) {
    auto this_opt = opt->cast<CNodePtr>();
    for (size_t i = 1; i < this_opt->inputs().size(); i++) {
      if (auto inp = this_opt->input(i); inp->isa<Parameter>()) {
        auto joint_opts = dfs(inp);
        if (joint_opts.size() != 1 || joint_opts.find(this_opt) == joint_opts.end()) {
          return false;
        }
      }
    }
  }
  return true;
}

void DoParallel(const std::vector<std::pair<AnfNodePtr, AnfNodePtr>> &updatestate_opts, const FuncGraphManagerPtr &mng,
                const AnfNodePtr &first_updatestate) {
  std::vector<AnfNodePtr> additional_inputs;
  for (size_t i = 0; i < updatestate_opts.size(); i++) {
    auto opt_cnode = updatestate_opts[i].second->cast<CNodePtr>();
    opt_cnode->set_input(opt_cnode->inputs().size() - 1, first_updatestate);
    if (i < updatestate_opts.size() - 1) {
      auto ups_cnode = updatestate_opts[i].first->cast<CNodePtr>();
      additional_inputs.insert(additional_inputs.end(), ups_cnode->inputs().begin() + REAL_NODE_START_POS,
                               ups_cnode->inputs().end());
    }
  }
  auto last_updatestate = updatestate_opts.back().first->cast<CNodePtr>();
  last_updatestate->set_input(UMONAD_POS, first_updatestate);
  std::vector<AnfNodePtr> final_inputs = last_updatestate->inputs();
  (void)final_inputs.insert(final_inputs.end(), additional_inputs.begin(), additional_inputs.end());
  last_updatestate->set_inputs(final_inputs);
}
}  // namespace

bool ParallelOptimizer::Run(const FuncGraphPtr &func_graph) {
  MS_EXCEPTION_IF_NULL(func_graph);
  MS_EXCEPTION_IF_NULL(func_graph->get_return());
  auto mng = func_graph->manager();
  MS_EXCEPTION_IF_NULL(mng);
  std::vector<PrimitivePtr> opt_list = {prim::kPrimAdamWeightDecay};
  bool graph_change = false;
  for (auto &opt_prim : opt_list) {
    auto todos = TopoSort(func_graph->get_return());
    mindspore::HashSet<AnfNodePtr> visited_updatestates;
    std::vector<std::pair<AnfNodePtr, AnfNodePtr>> updatestate_opts;
    bool changed = false;
    for (auto &node : todos) {
      // find pattern: updatestate->optimizer->updatestate->optimizer->...updatestate->optimizer->updatestate
      if (IsPrimitiveCNode(node, prim::kPrimUpdateState) &&
          visited_updatestates.find(node) == visited_updatestates.end()) {
        updatestate_opts.clear();
        (void)visited_updatestates.insert(node);
        auto res = IsTargetUpdateState(node, opt_prim, mng);
        while (res.first != nullptr) {
          (void)visited_updatestates.insert(res.first);
          (void)updatestate_opts.emplace_back(res);
          res = IsTargetUpdateState(updatestate_opts.back().first, opt_prim, mng);
        }
        mindspore::HashSet<AnfNodePtr> opts_set;
        (void)std::for_each(
          updatestate_opts.begin(), updatestate_opts.end(),
          [&opts_set](const std::pair<AnfNodePtr, AnfNodePtr> &p) { (void)opts_set.insert(p.second); });
        if (opts_set.size() > 1 && CanParallel(opts_set, mng)) {
          DoParallel(updatestate_opts, mng, node);
          changed = true;
        }
      }
    }
    if (changed) {
      graph_change = true;
      mng->RemoveRoots();
      mng->KeepRoots({func_graph});
    }
  }
  return graph_change;
}
}  // namespace mindspore::graphkernel
