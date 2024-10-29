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

#include <memory>
#include <unordered_map>
#include <vector>

#include "buffer_allocator.h"
#include "types.h"

namespace mooncake {

struct AllocationStrategyConfig {
    virtual ~AllocationStrategyConfig() = default;

    // 通用配置选项
    std::vector<SegmentId> source_segment_ids;  // write时，源数据的segment ids
    std::vector<SegmentId> des_segment_ids;  // read时，目的空间的segment ids
};

class AllocationStrategy {
   public:
    // 在buf_allocators中为shard编号为shard_index找到一个合适的segment
    // replica_list： 现在已经分配的副本数据
    // failed_segment_ids： 分配失败的segment，下次选择不能选择这个segment
    virtual SegmentId selectSegment(
        const BufferResources &buf_allocators, const ReplicaList &replica_list,
        const int shard_index, std::vector<SegmentId> &failed_segment_ids) = 0;

    // 从多个副本中挑选current_handle_index位置上的合适的BufHandle
    // failed_buffer : 不可访问的buffer
    virtual std::shared_ptr<BufHandle> selectHandle(
        const ReplicaList &replicas, size_t current_handle_index,
        std::vector<std::shared_ptr<BufHandle>> &failed_bufhandle) = 0;

    virtual void selected(SegmentId segment_id, int buf_index, size_t size) = 0;

    virtual void updateConfig(
        const std::shared_ptr<AllocationStrategyConfig> &config) = 0;

    virtual ~AllocationStrategy() = default;
};

}  // namespace mooncake
