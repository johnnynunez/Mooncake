#include <gflags/gflags.h>
#include <glog/logging.h>
#include <pybind11/pybind11.h>
#include <sys/time.h>

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <memory>

#include "transfer_engine.h"
#include "transport/rdma_transport/rdma_transport.h"
#include "transport/transport.h"

using namespace mooncake;

class VLLMAdaptor {
   public:
    const static size_t kDefaultBufferCapacity = 2ull * 1024 * 1024 * 1024;
    const static size_t kSlabSize = 4ull * 1024 * 1024;
    const static size_t kSlabCount = kDefaultBufferCapacity / kSlabSize;

    VLLMAdaptor();

    ~VLLMAdaptor();

    int initialize(const char *local_hostname, 
                   const char *metadata_server, 
                   const char *protocol,
                   const char *device_name);

    uintptr_t allocateManagedBuffer(size_t length);

    int freeManagedBuffer(uintptr_t user_tensor, size_t length);

    int transferSync(const char *target_hostname, uintptr_t buffer, uintptr_t peer_buffer_address, size_t length);

    int writeBytesToBuffer(uintptr_t dest_address, char *src_ptr, size_t length) {
        memcpy((void *) dest_address, (void *) src_ptr, length);
        return 0;
    }

    pybind11::bytes readBytesFromBuffer(uintptr_t source_address, size_t length) {
        return pybind11::bytes(static_cast<const char*>(reinterpret_cast<void*>(source_address)), length);
    }

   private:
    std::shared_ptr<TransferEngine> engine_;
    Transport *xport_;
    void *next_free_;
    void *managed_buffer_;
    std::unordered_set<void *> buffer_list_;
    std::mutex mutex_;
    std::unordered_map<std::string, Transport::SegmentHandle> handle_map_;
};
