#pragma once

#include "common_types.h"
#include "virtual_node.h"
#include "buffer_allocator.h"
#include <vector>
#include <memory>
#include <unordered_map>

namespace mooncake {


struct NodeInfo {
    std::string category;
    int segment_id;
    int allocator_index;
    size_t shard_size;

    NodeInfo(std::string cat, int seg, int alloc, size_t size)
        : category(std::move(cat)), segment_id(seg), allocator_index(alloc), shard_size(size) {}
};

using SelectNodesType = std::vector<NodeInfo>;

class AllocationStrategy
{
public:
    virtual std::vector<int> selectDummyNodes(int num_shards, int num_replicas, const std::vector<std::unique_ptr<VirtualNode>> &nodes) = 0;

    virtual  SelectNodesType selectNodes(
        int num_shards, 
        int num_replicas, 
        size_t shard_size,
        const BufferResources& buffer_resources) = 0;
    virtual ~AllocationStrategy() = default;
};

}
