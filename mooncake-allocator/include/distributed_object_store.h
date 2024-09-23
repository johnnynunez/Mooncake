#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "types.h"
#include "random_allocation_strategy.h"
#include "replica_allocator.h"

// #include "transfer_engine/transfer_engine.h"
#include "transfer_engine.h"
#include "transport/rdma_transport/rdma_transport.h"
#include "transport/transport.h"

namespace mooncake
{
    static std::string getHostname()
    {
        char hostname[256];
        if (gethostname(hostname, 256))
        {
            PLOG(ERROR) << "Failed to get hostname";
            return "";
        }
        return hostname;
    }

    // 定义 slice 结构体
    struct Slice {
        void* ptr;
        size_t size;
    };

    class DistributedObjectStore
    {
    public:
        struct ReplicaDiff
        {
            // 副本差异的具体实现
        };

        

        DistributedObjectStore();
        ~DistributedObjectStore();

        void *allocateLocalMemory(size_t buffer_size);

        void transferEngineInit();
        std::vector<void *> getLocalAddr()
        {
            return addr_;
        }

        uint64_t registerBuffer(SegmentId segment_id, size_t base, size_t size);
        void unregisterBuffer(SegmentId segment_id, uint64_t index);

        TaskID put(ObjectKey key, const std::vector<Slice>& slices, ReplicateConfig config);
    
        TaskID get(ObjectKey key, std::vector<Slice>& slices, Version min_version, size_t offset);

        TaskID remove(ObjectKey key, Version version = -1);

        TaskID replicate(ObjectKey key, ReplicateConfig new_config, ReplicaDiff &replica_diff);

        void checkAll();

        // private:
        uint64_t calculateObjectSize(const std::vector<void *> &ptrs);

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

        bool doWrite(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status);

        bool doDummyWrite(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status);

        bool doRead(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status);

        bool doDummyRead(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status);

        bool doReplica(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status);

        bool doDummyReplica(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status);

        int doTransfers(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status);

        void updateReplicaStatus(const std::vector<TransferRequest> &requests, const std::vector<TransferStatusEnum> &status,
                                 const std::string key, const Version version, ReplicaInfo &replica_info);

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

    private:
        std::unique_ptr<TransferEngine> transfer_engine_;
        RdmaTransport *rdma_engine_;
        std::vector<void *> addr_; // 本地存储
        const size_t dram_buffer_size_ = 1ull << 30;

        ReplicaAllocator replica_allocator_;
        std::shared_ptr<AllocationStrategy> allocation_strategy_;
        uint32_t max_trynum_;
    };

} // namespace mooncake
