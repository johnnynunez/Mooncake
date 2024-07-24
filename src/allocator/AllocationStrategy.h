#pragma once

#include "VirtualNode.h"
#include <vector>
#include <memory>

class AllocationStrategy
{
public:
    virtual std::vector<int> selectNodes(int num_shards, int num_replicas, const std::vector<std::unique_ptr<VirtualNode>> &nodes) = 0;
    virtual ~AllocationStrategy() = default;
};