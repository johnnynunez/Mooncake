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

    std::map<std::string, std::map<int, std::vector<int>>> RandomAllocationStrategy::selectNodes(
        int num_shards,
        int num_replicas,
        size_t shard_size,
        const BufferResources &buffer_resources) 
    {
        std::map<std::string, std::map<int, std::vector<int>>> selected_nodes;
        std::vector<std::tuple<std::string, int, int>> available_allocators;

        // 预处理：找出所有可用的 BufferAllocator
        for (const auto &[category, segments] : buffer_resources)
        {
            for (const auto &[segment_id, allocators] : segments)
            {
                for (int i = 0; i < allocators.size(); ++i)
                {
                    if (allocators[i].getRemainingSize() >= shard_size)
                    {
                        available_allocators.emplace_back(category, segment_id, i);
                    }
                }
            }
        }

        if (available_allocators.size() < num_shards * num_replicas)
        {
            throw std::runtime_error("Not enough available allocators to satisfy the request");
        }

        // 为每个副本分配 shards
        for (int replica = 0; replica < num_replicas; ++replica)
        {
            for (int shard = 0; shard < num_shards; ++shard)
            {
                if (available_allocators.empty())
                {
                    throw std::runtime_error("Ran out of available allocators during selection");
                }

                // 随机选择一个可用的 allocator
                std::uniform_int_distribution<> dis(0, available_allocators.size() - 1);
                int index = dis(rng_);
                auto [category, segment_id, allocator_index] = available_allocators[index];

                // 添加到结果中
                selected_nodes[category][segment_id].push_back(allocator_index);

                // 从可用列表中移除已选择的 allocator
                available_allocators.erase(available_allocators.begin() + index);
            }
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
