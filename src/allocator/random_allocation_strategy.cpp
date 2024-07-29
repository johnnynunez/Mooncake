#include "random_allocation_strategy.h"

#include <iostream>
#include <stdexcept>

namespace mooncake
{

    RandomAllocationStrategy::RandomAllocationStrategy()
    {
        std::random_device rd;
        rng_ = std::mt19937(rd());
    }

    SelectNodesType RandomAllocationStrategy::selectNodes(
        int num_shards,
        int num_replicas,
        size_t shard_size,
        const BufferResources &buffer_resources) 
    {
    std::vector<NodeInfo> selected_nodes;
    selected_nodes.reserve(num_shards * num_replicas);

    // 创建一个包含所有类别和段的向量，用于随机选择
    std::vector<std::pair<std::string, int>> all_segments;
    for (const auto &[category, segments] : buffer_resources) {
        for (const auto &[segment_id, allocators] : segments) {
            all_segments.emplace_back(category, segment_id);
        }
    }

    int total_allocations = num_shards * num_replicas;
    int attempts = 0;
    const int max_attempts = total_allocations * 10; // 设置一个最大尝试次数，避免无限循环

    while (selected_nodes.size() < total_allocations && attempts < max_attempts) {
        // 随机选择一个段
        std::uniform_int_distribution<> segment_dis(0, all_segments.size() - 1);
        int segment_index = segment_dis(rng_);
        const auto &[category, segment_id] = all_segments[segment_index];

        const auto &allocators = buffer_resources.at(category).at(segment_id);
        
        // 随机选择该段中的一个allocator
        std::uniform_int_distribution<> allocator_dis(0, allocators.size() - 1);
        int allocator_index = allocator_dis(rng_);

        if (allocators[allocator_index].getRemainingSize() >= shard_size) {
            selected_nodes.emplace_back(category, segment_id, allocator_index, shard_size);
        }

        attempts++;
    }

    if (selected_nodes.size() < total_allocations) {
        throw std::runtime_error("Failed to find enough available allocators after " + 
                                 std::to_string(max_attempts) + " attempts");
    }

    return selected_nodes;
}

    std::vector<int> RandomAllocationStrategy::selectDummyNodes(int num_shards, int num_replicas, const std::vector<std::unique_ptr<VirtualNode>> &nodes)
    {
        int num_virtual_nodes = nodes.size();
        if (num_shards * num_replicas > num_virtual_nodes)
        {
            std::cout << "num_shards: " << num_shards << ", num_replicas: " << num_replicas << ", num_virtual_nodes: " << num_virtual_nodes << std::endl;
            throw std::runtime_error("Not enough virtual nodes to allocate all shards and replicas");
        }

        std::vector<int> all_nodes(num_virtual_nodes);
        for (int i = 0; i < num_virtual_nodes; ++i)
        {
            all_nodes[i] = i;
        }

        std::vector<int> selected_nodes;
        selected_nodes.reserve(num_shards * num_replicas);

        for (int replica = 0; replica < num_replicas; ++replica)
        {
            std::vector<int> available_nodes = all_nodes;

            for (int shard = 0; shard < num_shards; ++shard)
            {
                if (available_nodes.empty())
                {
                    available_nodes = all_nodes;
                }

                std::uniform_int_distribution<> dis(0, available_nodes.size() - 1);
                int index = dis(rng_);
                int selected_node = available_nodes[index];

                selected_nodes.push_back(selected_node);

                available_nodes.erase(available_nodes.begin() + index);
            }
        }

        return selected_nodes;
    }

} // end namespace mooncake
