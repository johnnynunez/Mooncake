#pragma once

#include "AllocationStrategy.h"
#include <random>

class RandomAllocationStrategy : public AllocationStrategy
{
private:
    std::mt19937 rng;

public:
    RandomAllocationStrategy();
    std::vector<int> selectNodes(int num_shards, int num_replicas, const std::vector<std::unique_ptr<VirtualNode>> &nodes) override;
};