#pragma once

#include "types.h"
#include <vector>

#include "transfer_agent.h"
#include "transport/rdma_transport/rdma_transport.h"

namespace mooncake
{

    class RdmaTransferAgent : public TransferAgent
    {
    public:
        RdmaTransferAgent();
        ~RdmaTransferAgent() override;
        void init() override;
        SegmentId openSegment(const std::string &segment_name) override;
        void *allocateLocalMemory(size_t buffer_size) override;

        bool doWrite(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status) override;
        bool doRead(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status) override;
        bool doReplica(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status) override;

        bool doTransfers(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status) override;

        BatchID submitTransfersAsync(const std::vector<TransferRequest> &transfer_tasks);

    private:
        void monitorTransferStatus(BatchID batch_id, size_t task_count, std::vector<TransferStatusEnum> &transfer_status);

    private:
        std::unique_ptr<TransferEngine> transfer_engine_;
        RdmaTransport *rdma_engine_;
        std::vector<void *> addr_; // 本地存储
        const size_t dram_buffer_size_ = 1ull << 30;
    };

} // namespace mooncake