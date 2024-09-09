#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

#include "common_types.h"
#include "random_allocation_strategy.h"
#include "replica_allocator.h"

//#include "transfer_engine/transfer_engine.h"
#include "transfer_engine.h"

namespace mooncake {

static std::string getHostname() {
    char hostname[256];
    if (gethostname(hostname, 256)) {
        PLOG(ERROR) << "Failed to get hostname";
        return "";
    }
    return hostname;
}

class DistributedObjectStore {
public:
    struct ReplicaDiff {
        // 副本差异的具体实现
    };

    DistributedObjectStore();
    ~DistributedObjectStore();

    uint64_t registerBuffer(SegmentId segment_id, size_t base, size_t size);
    void unregisterBuffer(SegmentId segment_id, uint64_t index);

    TaskID put(ObjectKey key, std::vector<void*> ptrs, std::vector<void*> sizes, ReplicateConfig config);

    TaskID get(ObjectKey key, std::vector<void*> ptrs, std::vector<void*> sizes, Version min_version, size_t offset);

    TaskID replicate(ObjectKey key, ReplicateConfig new_config, ReplicaDiff& replica_diff);

    void checkAll();

// private:
    uint64_t calculateObjectSize(const std::vector<void*>& ptrs, const std::vector<void*>& sizes);

    void generateWriteTransferRequests(
        const ReplicaInfo& replica_info,
        const std::vector<void*>& ptrs,
        const std::vector<void*>& sizes,
        std::vector<TransferRequest>& transfer_tasks);

    void generateReadTransferRequests(
        const ReplicaInfo& replica_info,
        size_t offset,
        const std::vector<void*>& ptrs,
        const std::vector<void*>& sizes,
        std::vector<TransferRequest>& transfer_tasks);

    void generateReplicaTransferRequests(
        const ReplicaInfo& existed_replica_info,
        const ReplicaInfo& new_replica_info,
        std::vector<TransferRequest>& transfer_tasks);

    bool doWrite(const std::vector<TransferRequest>& transfer_tasks, std::vector<TransferStatusEnum>& status);

    bool doRead(const std::vector<TransferRequest>& transfer_tasks, std::vector<TransferStatusEnum>& status);

    bool doReplica(const std::vector<TransferRequest>& transfer_tasks, std::vector<TransferStatusEnum>& status);

    void updateReplicaStatus(const std::vector<TransferRequest>& requests, const std::vector<TransferStatusEnum>& status, 
    const std::string key, const Version version, ReplicaInfo& replica_info);

    bool validateTransferRequests(
        const ReplicaInfo& replica_info,
        const std::vector<void*>& ptrs,
        const std::vector<void*>& sizes,
        const std::vector<TransferRequest>& transfer_tasks);

    bool validateTransferReadRequests(
    const ReplicaInfo& replica_info,
    const std::vector<void*>& ptrs,
    const std::vector<void*>& sizes, // 使用void*来表示大小
    const std::vector<TransferRequest>& transfer_tasks);

private:
    std::unique_ptr<TransferEngine> transfer_engine_;
    ReplicaAllocator replica_allocator_;
    std::shared_ptr<AllocationStrategy> allocation_strategy_;
    int max_trynum_;
};

}  // namespace mooncake
