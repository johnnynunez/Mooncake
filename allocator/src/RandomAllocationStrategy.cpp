#include "RandomAllocationStrategy.h"
#include <iostream>
#include <stdexcept>

RandomAllocationStrategy::RandomAllocationStrategy()
{
    std::random_device rd;
    rng = std::mt19937(rd());
}

std::vector<int> RandomAllocationStrategy::selectNodes(int num_shards, int num_replicas, const std::vector<std::unique_ptr<VirtualNode>> &nodes)
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
            int index = dis(rng);
            int selected_node = available_nodes[index];

            selected_nodes.push_back(selected_node);

            available_nodes.erase(available_nodes.begin() + index);
        }
    }

    return selected_nodes;
}