#pragma once

#include "allocation_strategy.h"

#include <random>

namespace mooncake
{

    struct RandomAllocationStrategyConfig : AllocationStrategyConfig
    {
        int random_seed;

        // 实现深拷贝
        // std::unique_ptr<AllocationStrategyConfig> clone() const override {
        //     return std::make_unique<RandomAllocationStrategyConfig>(*this);
        // }
    };

    class RandomAllocationStrategy : public AllocationStrategy
    {
    private:
        std::mt19937 rng_;

    public:
        explicit RandomAllocationStrategy(std::shared_ptr<RandomAllocationStrategyConfig> initial_config);

        SegmentId selectSegment(
            const BufferResources &buf_allocators,
            const ReplicaList &replica_list,
            const int shard_index,
            std::vector<SegmentId> &failed_segment_ids) override;

        std::shared_ptr<BufHandle> selectHandle(
            const ReplicaList &replicas,
            size_t current_handle_index,
            std::vector<std::shared_ptr<BufHandle>> &failed_bufhandle) override;

        void selected(SegmentId segment_id, int buf_index, size_t size) override;

        void updateConfig(const std::shared_ptr<AllocationStrategyConfig> &new_config) override;

        ~RandomAllocationStrategy() override;

    private:
        std::shared_ptr<RandomAllocationStrategyConfig> config_;
    };

} // end namespace mooncake
