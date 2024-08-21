#ifndef RDMA_TRANSPORT_H_
#define RDMA_TRANSPORT_H_


#include "transport.h"

namespace mooncake {
    class RDMATransport : public Transport
    {
    public:
        BatchID allocateBatchID(size_t batch_size) override
        {
            std::cout << "allocateBatchID, batch_size: " << batch_size << std::endl;
            return 0x7fffffffffffffff;
        }

        int freeBatchID(BatchID batch_id) override
        {
            std::cout << "freeBatchID, batch_id: " << batch_id << std::endl;
            return 0;
        }

        int submitTransfer(BatchID batch_id, const std::vector<TransferRequest> &entries) override
        {
            std::cout << "submitTransfer, batch_id: " << batch_id << ", entries.size: " << entries.size() << std::endl;
            return entries.size();
        }

        /// @brief Get the status of a submitted transfer. This function shall not be called again after completion.
        /// @return Return 1 on completed (either success or failure); 0 if still in progress.
        int getTransferStatus(BatchID batch_id, size_t task_id, TransferStatus &status) override
        {
            std::cout << "getTransferStatus, batch_id: " << batch_id << ", task_id: " << task_id << std::endl;
            status.s = COMPLETED;
            status.transferred_bytes = 100;
            return 1;
        }

    private:
        int install(std::string &local_server_name, std::shared_ptr<TransferMetadata> meta, void **args) override
        {
            int rc = Transport::install(local_server_name, meta, args);
            // 1. get
            char *arg1 = (char *)args[0];
            std::cout << "rdma install, arg: " << arg1;
            return 0;
        }

        int registerLocalMemory(void *addr, size_t length, const string &location) override
        {
            std::cout << "registerLocalMemory, addr: " << addr << ", length: " << length << ", location: " << location << std::endl;
            return 0;
        }

        int unregisterLocalMemory(void *addr) override
        {
            std::cout << "unregisterLocalMemory, addr: " << addr << std::endl;
            return 0;
        }

        const char *getName() const override
        {
            return "rdma";
        }
    };
}

#endif