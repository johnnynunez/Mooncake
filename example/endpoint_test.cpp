#include "transfer_engine/rdma_context.h"
#include "transfer_engine/transfer_engine.h"

#include <cstdlib>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <iomanip>
#include <string>
#include <sys/time.h>

using namespace mooncake;

static std::string getHostname()
{
    char hostname[256];
    if (gethostname(hostname, 256))
    {
        PLOG(ERROR) << "Failed to get hostname";
        return "";
    }
    return hostname;
}


int main(int argc, char **argv)
{
    auto metadata_client = std::make_unique<TransferMetadata>("dummy");
    // malloc and set name to bypass transfer engine init
    auto engine = std::make_unique<TransferEngine>(metadata_client, getHostname(),
                                                   "", true);
    RdmaContext* content = new RdmaContext(*engine.get(), "mlx5_0");
    content->construct();
    for (int i = 0; i < 500; ++i) {
      LOG(INFO) << "ep " << i;
      content->endpoint(std::to_string(i));
    }
    return 0;
}
