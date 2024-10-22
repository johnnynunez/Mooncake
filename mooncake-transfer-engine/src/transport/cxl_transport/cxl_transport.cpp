#include "transport/cxl_transport/cxl_transport.h"
#include "common.h"
#include "transfer_engine.h"
#include "transfer_metadata.h"
#include "transport/transport.h"
#include <algorithm>
#include <bits/stdint-uintn.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <glog/logging.h>
#include <iomanip>
#include <memory>

namespace mooncake
{
    CxlTransport::CxlTransport()
    {
        // TODO
    }

    CxlTransport::~CxlTransport() {}

    CxlTransport::BatchID CxlTransport::allocateBatchID(size_t batch_size)
    {
        auto batch_id = Transport::allocateBatchID(batch_size);
        return batch_id;
    }

    int CxlTransport::getTransferStatus(BatchID batch_id, size_t task_id, TransferStatus &status)
    {
        return 0;
    }

    int CxlTransport::submitTransfer(BatchID batch_id, const std::vector<TransferRequest> &entries)
    {
        return 0;
    }

    int CxlTransport::freeBatchID(BatchID batch_id)
    {
        return 0;
    }

    int CxlTransport::install(std::string &local_server_name, std::shared_ptr<TransferMetadata> meta, void **args)
    {
        return 0;
    }

    int CxlTransport::registerLocalMemory(void *addr, size_t length, const string &location, bool remote_accessible, bool update_metadata)
    {
        return 0;
    }

    int CxlTransport::unregisterLocalMemory(void *addr, bool update_metadata)
    {
        return 0;
    }
}