#ifndef NVMEOF_TRANSPORT_H_
#define NVMEOF_TRANSPORT_H_

#include "transfer_engine/cufile_context.h"
#include "transfer_engine/transfer_metadata.h"
#include "transport.h"
#include <bits/stdint-uintn.h>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace mooncake
{
    class NVMeoFTransport : public Transport
    {
    public:
        BatchID allocateBatchID(size_t batch_size) override;

        int submitTransfer(BatchID batch_id, const std::vector<TransferRequest> &entries) override;

        int getTransferStatus(BatchID batch_id, size_t task_id, TransferStatus &status) override;

    private:
        struct CuFileBatchDesc
        {
            CUfileBatchHandle_t handle;
            std::vector<CUfileIOParams_t> cufile_io_params;
            std::vector<CUfileIOEvents_t> cufile_events_buf;
            std::vector<TransferStatus> transfer_status;
            unsigned nr_completed;
        };

        struct pair_hash
        {
            template <class T1, class T2>
            std::size_t operator()(const std::pair<T1, T2> &pair) const
            {
                auto hash1 = std::hash<T1>{}(pair.first);
                auto hash2 = std::hash<T2>{}(pair.second);
                return hash1 ^ hash2; // 结合两个哈希值
            }
        };

        int install(std::string &local_server_name, std::shared_ptr<TransferMetadata> meta, void **args) override;

        int registerLocalMemory(void *addr, size_t length, const string &location) override;

        int unregisterLocalMemory(void *addr) override;

        const char *getName() const override { return "nvmeof"; }

        std::unordered_map<std::pair<SegmentHandle, uint64_t>, std::shared_ptr<CuFileContext>, pair_hash> segment_to_context_;
        // TODO: make this <seg_id, buf_id> to context
        const std::string local_server_name_;
        std::shared_ptr<TransferMetadata> meta_;
        std::vector<std::thread> workers;

        std::unordered_map<BatchID, BatchDesc> batch_map_;
        // CuFileContext* context_  ;
        // CUfileBatchHandle_t handle = NULL;
        // std::vector<CUfileIOParams_t> cufile_io_params;
        // std::vector<CUfileIOEvents_t> cufile_events_buf;
        // std::vector<TransferStatus> transfer_status;
        unsigned nr_completed = 0;
    };
}

#endif