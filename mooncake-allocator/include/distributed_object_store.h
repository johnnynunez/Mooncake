#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "random_allocation_strategy.h"
#include "replica_allocator.h"
#include "types.h"

#include "transfer_agent.h"
#include "transfer_engine.h"
#include "transport/rdma_transport/rdma_transport.h"
#include "transport/transport.h"

namespace mooncake
{
    struct Slice
    {
        void *ptr;
        size_t size;
    };

    struct PairHash
    {
        template <class T1, class T2>
        std::size_t operator()(const std::pair<T1, T2> &p) const
        {
            auto h1 = std::hash<T1>{}(p.first);
            auto h2 = std::hash<T2>{}(p.second);
            return h1 ^ (h2 << 1);
        }
    };

    // define operation type
    enum class OperationType : uint8_t
    {
        PUT,
        GET,
        REMOVE,
        REPLICATE
    };

    class DistributedObjectStore
    {
    public:
        struct ReplicaDiff
        {
            // diff of replica
        };

        struct TaskContext
        {
            TaskID task_id;
            OperationType type;
            ObjectKey key;
            TASK_STATUS task_status;
            Version version;
            BatchID batch_id;
            int replica_num;
            std::vector<ReplicaInfo> replica_infos;
            std::vector<std::vector<TransferRequest>> all_requests;
            size_t task_size;
        };

        DistributedObjectStore();

        ~DistributedObjectStore();

        void *allocateLocalMemory(size_t buffer_size);

        SegmentId openSegment(const std::string &segment_name);

        uint64_t registerBuffer(SegmentId segment_id, size_t base, size_t size);

        void unregisterBuffer(SegmentId segment_id, uint64_t index);

        TaskID put(ObjectKey key, const std::vector<Slice> &slices, ReplicateConfig config);

        TaskID get(ObjectKey key, std::vector<Slice> &slices, Version min_version, size_t offset);

        TaskID remove(ObjectKey key, Version version = -1);

        TaskID replicate(ObjectKey key, ReplicateConfig new_config, ReplicaDiff &replica_diff);

        void checkAll();

        TASK_STATUS getTaskStatus(TaskID task_id);

        void generateWriteTransferRequests(
            const ReplicaInfo &replica_info,
            const std::vector<Slice> &slices,
            std::vector<TransferRequest> &transfer_tasks);

        void generateReadTransferRequests(
            const ReplicaInfo &replica_info,
            size_t offset,
            const std::vector<Slice> &slices,
            std::vector<TransferRequest> &transfer_tasks);

        void generateReplicaTransferRequests(
            const ReplicaInfo &existed_replica_info,
            const ReplicaInfo &new_replica_info,
            std::vector<TransferRequest> &transfer_tasks);

    private:
        TASK_STATUS updatePutStatus(TaskID task_id, std::vector<TransferStatusEnum> &status);

        uint64_t calculateObjectSize(const std::vector<void *> &ptrs);

        void updateReplicaStatus(const std::vector<TransferRequest> &requests, const std::vector<TransferStatusEnum> &status,
                                 const std::string &key, const Version version, ReplicaInfo &replica_info);

        bool validateTransferRequests(
            const ReplicaInfo &replica_info,
            const std::vector<void *> &ptrs,
            const std::vector<void *> &sizes,
            const std::vector<TransferRequest> &transfer_tasks);

        bool validateTransferRequests(
            const ReplicaInfo &replica_info,
            const std::vector<Slice> &slices,
            const std::vector<TransferRequest> &transfer_tasks);

        bool validateTransferReadRequests(
            const ReplicaInfo &replica_info,
            const std::vector<Slice> &slices,
            const std::vector<TransferRequest> &transfer_tasks);

        void handlePutCompletion(
            std::shared_ptr<TaskContext> context,
            const std::vector<TransferStatusEnum> &status);

    private:
        // define atomic taskid
        std::atomic<TaskID> task_id_;
        std::unordered_map<TaskID,
                           std::shared_ptr<TaskContext>>
            task_contexts_;

        ReplicaAllocator replica_allocator_;
        std::shared_ptr<AllocationStrategy> allocation_strategy_;
        uint32_t max_trynum_;

        std::unique_ptr<TransferAgent> transfer_agent_;
    };

} // namespace mooncake
