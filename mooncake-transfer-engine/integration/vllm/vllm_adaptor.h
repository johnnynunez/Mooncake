#include <gflags/gflags.h>
#include <glog/logging.h>
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
    const static size_t kMaxBufferSize = 4 * 1024 * 1024;
    const static size_t kBufferCount = 32;

    VLLMAdaptor();

    ~VLLMAdaptor();

    int initialize(const char *local_hostname, const char *metadata_server,
                   const char *protocol, const char *device_name);

    void *allocateManagedBuffer(size_t length);

    int freeManagedBuffer(void *user_tensor, size_t length);

    int transferSync(const char *target_hostname, void *buffer,
                     uint64_t peer_buffer_address, size_t length);

   private:
    std::shared_ptr<TransferEngine> engine_;
    Transport *xport_;
    void *next_free_;
    void *managed_buffer_;
    std::unordered_set<void *> buffer_list_;
    std::mutex mutex_;
    std::unordered_map<std::string, Transport::SegmentHandle> handle_map_;
};
