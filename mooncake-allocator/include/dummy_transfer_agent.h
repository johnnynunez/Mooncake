#pragma once

#include "types.h"
#include <vector>

#include "transfer_agent.h"

namespace mooncake
{

    class DummyTransferAgent : public TransferAgent
    {
    public:
        void init() override;
        void *allocateLocalMemory(size_t buffer_size) override;
        SegmentId openSegment(const std::string &segment_name) override;
        bool doWrite(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status) override;
        bool doRead(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status) override;
        bool doReplica(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status) override;
        bool doTransfers(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status) override;
        BatchID submitTransfersAsync(const std::vector<TransferRequest> &transfer_tasks) override;
        void monitorTransferStatus(BatchID batch_id, size_t task_count, std::vector<TransferStatusEnum> &transfer_status) override;

    private:
        std::vector<void *> addr_; // 本地存储
    };
} // namespace mooncake