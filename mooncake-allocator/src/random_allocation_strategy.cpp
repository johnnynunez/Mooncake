#include "random_allocation_strategy.h"

#include <iostream>
#include <stdexcept>
#include <unordered_set>

namespace mooncake
{

    RandomAllocationStrategy::RandomAllocationStrategy(std::shared_ptr<RandomAllocationStrategyConfig> initial_config)
        : config_(std::move(initial_config))
    {
        if (!config_)
        {
            throw std::invalid_argument("Initial config cannot be null");
        }
    }

    SegmentId RandomAllocationStrategy::selectSegment(
        const BufferResources &buf_allocators,
        const ReplicaList &replica_list,
        const int shard_index,
        std::vector<SegmentId> &failed_segment_ids)
    {
        std::unordered_set<SegmentId> existing_segments;
        for (const auto &[replica_id, replica] : replica_list)
        {
            if (shard_index >= (int)replica.handles.size())
            {
                LOG(ERROR) << "wrong shard_index: " << shard_index << " in selectSegment";
                return getError(ERRNO::SHARD_INDEX_OUT_OF_RANGE);
            }
            existing_segments.insert(replica.handles[shard_index]->segment_id);
        }

        std::vector<SegmentId> available_segments;
        for (const auto &[segment_id, allocators] : buf_allocators)
        {
            if (existing_segments.find(segment_id) == existing_segments.end())
            {
                available_segments.push_back(segment_id);
            }
        }
        // print existing_segments and available_segments
        for (const auto &segment_id : existing_segments)
        {
            LOG(INFO) << "Existing segment: " << segment_id;
        }
        for (const auto &segment_id : available_segments)
        {
            LOG(INFO) << "Available segment: " << segment_id;
        }
        LOG(INFO) << "the failed segment size: " << failed_segment_ids.size();
        if (available_segments.empty())
        {
            LOG(WARNING) << "No available segments found";
            return getError(ERRNO::AVAILABLE_SEGMENT_EMPTY);
        }

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, available_segments.size() - 1);

        return available_segments[dis(gen)];
    }

    void RandomAllocationStrategy::selected(SegmentId segment_id, int buf_index, size_t size)
    {
        // Implement if needed
        LOG(INFO) << "selected segment_id: " << segment_id << ", buf_index: " << buf_index << ", size: " << size;
        return;
    }

    std::shared_ptr<BufHandle> RandomAllocationStrategy::selectHandle(
        const ReplicaList &replicas,
        size_t current_handle_index,
        std::vector<std::shared_ptr<BufHandle>> &failed_bufhandle)
    {
        if (replicas.empty())
        {
            throw std::runtime_error("No replicas available");
        }
        int trynum = 0;
        int max_try_num = 100;
        for (; trynum < max_try_num; ++trynum)
        {
            std::uniform_int_distribution<size_t> dist(0, replicas.size() - 1);
            size_t random_index = dist(rng_);
            const ReplicaInfo &replica = replicas.at(random_index);
            auto selected_hendle = replica.handles[current_handle_index];
            if (selected_hendle->status == BufStatus::COMPLETE)
            {
                return selected_hendle;
            }
        }
        LOG(ERROR) << "cannot select right handle, current_handle_index: " << current_handle_index << ", the failed_bufhandle size: " << failed_bufhandle.size();
        return nullptr;
    }

    void RandomAllocationStrategy::updateConfig(const std::shared_ptr<AllocationStrategyConfig> &new_config)
    {
        auto cast_config = std::dynamic_pointer_cast<RandomAllocationStrategyConfig>(new_config);
        if (!cast_config)
        {
            throw std::invalid_argument("Invalid config type");
        }
        config_ = cast_config;
    }

    RandomAllocationStrategy::~RandomAllocationStrategy() {}

} // end namespace mooncake
