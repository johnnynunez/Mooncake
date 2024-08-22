#ifndef MULTI_TRANSFER_ENGINE_H_
#define MULTI_TRANSFER_ENGINE_H_

#include <asm-generic/errno-base.h>
#include <bits/stdint-uintn.h>
#include <cstddef>
#include <cstdint>
#include <limits.h>
#include <string.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "nvmeof_transport.h"
#include "rdma_transport.h"
#include "transfer_engine/transfer_engine.h"
#include "transfer_engine/transfer_metadata.h"
#include "transport.h"

namespace mooncake
{
    class TransferEnginev2
    {
    public:
        TransferEnginev2(std::shared_ptr<TransferMetadata> meta) : meta_(meta) {}

        ~TransferEnginev2()
        {
            freeEngine();
        }

        int init(const char *server_name, const char *connectable_name, uint64_t rpc_port = 12345);

        int freeEngine();

        Transport *installOrGetTransport(const char *proto, void **args);

        int uninstallTransport(const char *proto);

        Transport::SegmentHandle openSegment(const char *segment_name);

        int closeSegment(Transport::SegmentHandle seg_id);

        int registerLocalMemory(void *addr, size_t length, const std::string &location, bool remote_accessible = false);

        int unregisterLocalMemory(void *addr);

    private:
        Transport *findName(const char *name, size_t n = SIZE_MAX);

        Transport *initTransport(const char *proto);

        std::vector<Transport *> installed_transports_;
        string local_server_name_;
        std::shared_ptr<TransferMetadata> meta_;
    };
}

#endif