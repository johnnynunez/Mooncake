#ifndef TCP_TRANSPORT_H_
#define TCP_TRANSPORT_H_

#include <infiniband/verbs.h>

#include <atomic>
#include <boost/asio.hpp>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "transfer_metadata.h"
#include "transport/transport.h"

namespace mooncake {
class TransferMetadata;
class TcpContext;

class TcpTransport : public Transport {
   public:
    using BufferDesc = TransferMetadata::BufferDesc;
    using SegmentDesc = TransferMetadata::SegmentDesc;
    using HandShakeDesc = TransferMetadata::HandShakeDesc;

   public:
    TcpTransport();

    ~TcpTransport();

    int submitTransfer(BatchID batch_id,
                       const std::vector<TransferRequest> &entries) override;

    int getTransferStatus(BatchID batch_id, size_t task_id,
                          TransferStatus &status) override;

   private:
    int install(std::string &local_server_name,
                std::shared_ptr<TransferMetadata> meta, void **args) override;

    int allocateLocalSegmentID();

    int registerLocalMemory(void *addr, size_t length, const string &location,
                            bool remote_accessible, bool update_metadata);

    int unregisterLocalMemory(void *addr, bool update_metadata = false);

    int registerLocalMemoryBatch(
        const std::vector<Transport::BufferEntry> &buffer_list,
        const std::string &location);

    int unregisterLocalMemoryBatch(
        const std::vector<void *> &addr_list) override;

    void worker();

    void startTransfer(Slice *slice);

    const char *getName() const override { return "tcp"; }

   private:
    TcpContext *context_;
    std::atomic_bool running_;
    std::thread thread_;
};
}  // namespace mooncake

#endif