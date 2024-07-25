#pragma once

#include "common_types.h"
#include "virtual_node.h"
#include "buffer_allocator.h"
#include <vector>
#include <memory>

namespace mooncake {

class AllocationStrategy
{
public:
    virtual std::vector<int> selectDummyNodes(int num_shards, int num_replicas, const std::vector<std::unique_ptr<VirtualNode>> &nodes) = 0;

    virtual  std::map<std::string, std::map<int, std::vector<int>>> selectNodes(
        int num_shards, 
        int num_replicas, 
        size_t shard_size,
        const BufferResources& buffer_resources) = 0;
    virtual ~AllocationStrategy() = default;
};

}
