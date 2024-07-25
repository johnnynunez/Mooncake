#pragma once

#include "allocation_strategy.h"

#include <random>

namespace mooncake {

class RandomAllocationStrategy : public AllocationStrategy
{
private:
    std::mt19937 rng_;

public:
    RandomAllocationStrategy();
     std::map<std::string, std::map<int, std::vector<int>>> selectNodes(
        int num_shards, 
        int num_replicas, 
        size_t shard_size,
        const BufferResources& buffer_resources) override;

    std::vector<int> selectDummyNodes(int num_shards, int num_replicas, const std::vector<std::unique_ptr<VirtualNode>> &nodes) override;
};

} // end namespace mooncake
