#include "transfer_engine/cufile_context.h"
#include "transport.h"
#include <unordered_map>

namespace mooncake
{
    class NVMeoFTransport : public Transport
    {
    public:
        BatchID allocateBatchID(size_t batch_size) override;

        int freeBatchID(BatchID batch_id) override;

        int submitTransfer(BatchID batch_id, const std::vector<TransferRequest> &entries) override;

        int getTransferStatus(BatchID batch_id, size_t task_id, TransferStatus &status) override;

    private:
        struct BatchDesc
        {
            BatchID id;
            size_t batch_size;
            std::vector<TransferTask> task_list;
            CUfileBatchHandle_t handle = NULL;
            std::vector<CUfileIOParams_t> cufile_io_params;
            std::vector<CUfileIOEvents_t> cufile_events_buf;
            std::vector<TransferStatus> transfer_status;
            unsigned nr_completed = 0;
        };

        int install(void **args) override;

        int registerLocalMemory(void *addr, size_t length, const string &location) override;

        int unregisterLocalMemory(void *addr) override;

        const char *getName() const override { return "nvmeof"; }

        std::unordered_map<SegmentHandle, CuFileContext> segment_to_context_;
    };
}