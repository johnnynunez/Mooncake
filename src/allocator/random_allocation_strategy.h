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
    std::vector<int> selectNodes(int num_shards, int num_replicas, const std::vector<std::unique_ptr<VirtualNode>> &nodes) override;
};

} // end namespace mooncake
