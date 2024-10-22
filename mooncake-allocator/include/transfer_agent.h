#pragma once

#include "types.h"
#include <vector>

#include "transfer_engine.h"
#include "transport/transport.h"

namespace mooncake
{
    using TransferCallback = std::function<void(const std::vector<TransferStatusEnum> &)>;
    class TransferAgent
    {
    public:
        TransferAgent() = default;
        virtual ~TransferAgent() = default;
        virtual void init() = 0;
        virtual void *allocateLocalMemory(size_t buffer_size) = 0;
        virtual SegmentId openSegment(const std::string &segment_name) = 0;

        virtual bool doWrite(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status) = 0;
        virtual bool doRead(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status) = 0;
        virtual bool doReplica(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status) = 0;
        virtual bool doTransfers(const std::vector<TransferRequest> &transfer_tasks, std::vector<TransferStatusEnum> &transfer_status) = 0;
        virtual BatchID submitTransfersAsync(const std::vector<TransferRequest> &transfer_tasks) = 0;

        virtual void monitorTransferStatus(BatchID batch_id, size_t task_count, std::vector<TransferStatusEnum> &transfer_status) = 0;
    };

} // namespace mooncake