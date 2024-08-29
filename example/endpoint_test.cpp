#include "rdma_context.h"
#include "transfer_engine/transfer_engine.h"

#include <cstdlib>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <iomanip>
#include <string>
#include <sys/time.h>
#include <thread>
#include <vector>

using namespace mooncake;

const int NR_THREADS = 8;

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

void test_endpoint(RdmaContext *content)
{
    for (int i = 0; i < 256; ++i)
    {
        LOG(INFO) << "ep " << i;
        content->endpoint(std::to_string(i));
    }
    for (int i = 0; i < 128; ++i)
    {
        LOG(INFO) << "ep " << i;
        content->endpoint(std::to_string(i));
    }
    for (int i = 128; i < 256; i += 2)
    {
        LOG(INFO) << "ep " << i;
        content->endpoint(std::to_string(i));
    }
    for (int i = 256; i < 512; ++i)
    {
        LOG(INFO) << "ep " << i;
        content->endpoint(std::to_string(i));
    }
    for (int i = 128; i < 256; ++i)
    {
        LOG(INFO) << "ep " << i;
        content->endpoint(std::to_string(i));
    }
}

int main(int argc, char **argv)
{
    auto metadata_client = std::make_unique<TransferMetadata>("dummy");
    // malloc and set name to bypass transfer engine init
    auto engine = std::make_unique<TransferEngine>(metadata_client, getHostname(),
                                                   "", true);
    RdmaContext *content = new RdmaContext(*engine.get(), "mlx5_0");
    content->construct();

    std::vector<std::thread> threads;
    for (int i = 0; i < NR_THREADS; ++i)
    {
        threads.push_back(std::thread(test_endpoint, content));
    }
    for (auto &t : threads)
    {
        t.join();
    }
    return 0;
}
