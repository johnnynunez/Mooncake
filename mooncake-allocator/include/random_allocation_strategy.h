// Copyright 2024 KVCache.AI
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <random>

#include "allocation_strategy.h"

namespace mooncake {

struct RandomAllocationStrategyConfig : AllocationStrategyConfig {
    int random_seed;

    // 实现深拷贝
    // std::unique_ptr<AllocationStrategyConfig> clone() const override {
    //     return std::make_unique<RandomAllocationStrategyConfig>(*this);
    // }
};

class RandomAllocationStrategy : public AllocationStrategy {
   private:
    std::mt19937 rng_;

   public:
    explicit RandomAllocationStrategy(
        std::shared_ptr<RandomAllocationStrategyConfig> initial_config);

    SegmentId selectSegment(
        const BufferResources &buf_allocators, const ReplicaList &replica_list,
        const int shard_index,
        std::vector<SegmentId> &failed_segment_ids) override;

    std::shared_ptr<BufHandle> selectHandle(
        const ReplicaList &replicas, size_t current_handle_index,
        std::vector<std::shared_ptr<BufHandle>> &failed_bufhandle) override;

    void selected(SegmentId segment_id, int buf_index, size_t size) override;

    void updateConfig(
        const std::shared_ptr<AllocationStrategyConfig> &new_config) override;

    ~RandomAllocationStrategy() override;

   private:
    std::shared_ptr<RandomAllocationStrategyConfig> config_;
};

}  // end namespace mooncake
