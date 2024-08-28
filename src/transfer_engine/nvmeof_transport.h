#ifndef NVMEOF_TRANSPORT_H_
#define NVMEOF_TRANSPORT_H_

#include "transfer_engine/cufile_context.h"
#include "transfer_engine/cufile_desc_pool.h"
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
        NVMeoFTransport();

        ~NVMeoFTransport();

        BatchID allocateBatchID(size_t batch_size) override;

        int submitTransfer(BatchID batch_id, const std::vector<TransferRequest> &entries) override;

        int getTransferStatus(BatchID batch_id, size_t task_id, TransferStatus &status) override;

        int freeBatchID(BatchID batch_id) override;

    private:
        struct NVMeoFBatchDesc
        {
            size_t desc_idx_;
            std::vector<TransferStatus> transfer_status;
            std::vector<std::pair<uint64_t, uint64_t>> task_to_slices; // task id -> (slice_begin, slice_num)
            // unsigned nr_completed;
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

        int registerLocalMemory(void *addr, size_t length, const string &location, bool remote_accessible) override;

        int unregisterLocalMemory(void *addr, bool update_metadata = false) override;

        int registerLocalMemoryBatch(const std::vector<Transport::BufferEntry> &buffer_list, const std::string &location) override { return 0; }

        int unregisterLocalMemoryBatch(const std::vector<void *> &addr_list) override { return 0; }

        const char *getName() const override { return "nvmeof"; }

        std::unordered_map<std::pair<SegmentHandle, uint64_t>, std::shared_ptr<CuFileContext>, pair_hash> segment_to_context_;
        std::vector<std::thread> workers_;
        std::shared_ptr<CUFileDescPool> desc_pool_;

        std::unordered_map<BatchID, BatchDesc> batch_map_;
        unsigned nr_completed = 0;
        CUfileBatchHandle_t handle = NULL;
    };
}

#endif