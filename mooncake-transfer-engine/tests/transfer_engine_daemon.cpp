// Sample transfer engine daemon

#include "transport/rdma_transport/rdma_transport.h"
#include "transfer_engine.h"
#include "transport/transport.h"

#include <cstdlib>
#include <fstream>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <iomanip>
#include <memory>
#include <sys/time.h>

#define NR_SOCKETS (1)
#define BASE_ADDRESS_HINT (0x40000000000)

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

DEFINE_string(local_server_name, getHostname(), "Local server name for segment discovery");
DEFINE_string(metadata_server, "optane21:2379", "etcd server host address");
DEFINE_string(nic_priority_matrix, "{\"cpu:0\": [[\"mlx5_2\"], []], \"cpu:1\": [[\"mlx5_2\"], []]}", "NIC priority matrix");

using namespace mooncake;

static void *allocateMemoryPool(size_t size, int socket_id)
{
    void *start_addr;
    start_addr = mmap((void *) BASE_ADDRESS_HINT, size, PROT_READ | PROT_WRITE,
                      MAP_ANON | MAP_PRIVATE,
                      -1, 0);
    if (start_addr == MAP_FAILED)
    {
        PLOG(ERROR) << "Failed to allocate memory";
        return nullptr;
    }
    return start_addr;
}

static void freeMemoryPool(void *addr, size_t size)
{
    munmap(addr, size);
}

std::string loadNicPriorityMatrix(const std::string &path)
{
    std::ifstream file(path);
    if (file.is_open())
    {
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();
        return content;
    }
    else
    {
        return path;
    }
}

int target()
{
    auto metadata_client = std::make_shared<TransferMetadata>(FLAGS_metadata_server);
    LOG_ASSERT(metadata_client);

    auto nic_priority_matrix = loadNicPriorityMatrix(FLAGS_nic_priority_matrix);

    const size_t dram_buffer_size = 1ull << 30;
    auto engine = std::make_unique<TransferEngine>(metadata_client);

    void** args = (void**) malloc(2 * sizeof(void*));
    args[0] = (void*)FLAGS_nic_priority_matrix.c_str();
    args[1] = nullptr;

    const string& connectable_name = FLAGS_local_server_name;
    engine->init(FLAGS_local_server_name.c_str(), connectable_name.c_str(), 12345);
    engine->installOrGetTransport("rdma", args);

    LOG_ASSERT(engine);

    void *addr[2] = {nullptr};
    for (int i = 0; i < NR_SOCKETS; ++i)
    {
        addr[i] = allocateMemoryPool(dram_buffer_size, i);
        int rc = engine->registerLocalMemory(addr[i], dram_buffer_size, "cpu:" + std::to_string(i));
        LOG_ASSERT(!rc);
    }

    while (true)
        sleep(1);

    for (int i = 0; i < NR_SOCKETS; ++i)
    {
        engine->unregisterLocalMemory(addr[i]);
        freeMemoryPool(addr[i], dram_buffer_size);
    }

    return 0;
}

int main(int argc, char **argv)
{
    gflags::ParseCommandLineFlags(&argc, &argv, false);
    return target();
}
