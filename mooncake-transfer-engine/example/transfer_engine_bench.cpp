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

#define NR_SOCKETS (2)

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
DEFINE_string(mode, "initiator",
              "Running mode: initiator or target. Initiator node read/write "
              "data blocks from target node");
DEFINE_string(operation, "read", "Operation type: read or write");

DEFINE_string(device_name, "mlx5_2", "Device name to use");
DEFINE_string(nic_priority_matrix, "", "Path to NIC priority matrix file (Advanced)");

DEFINE_string(segment_id, "optane20", "Segment ID to access data");
DEFINE_int32(batch_size, 128, "Batch size");
DEFINE_int32(block_size, 4096, "Block size for each transfer request");
DEFINE_int32(duration, 10, "Test duration in seconds");
DEFINE_int32(threads, 4, "Task submission threads");

using namespace mooncake;

static void *allocateMemoryPool(size_t size, int socket_id)
{
    return numa_alloc_onnode(size, socket_id);
}

static void freeMemoryPool(void *addr, size_t size)
{
#ifdef USE_CUDA
    // check pointer on GPU
    CUresult cu_result;
    CUpointer_attribute attributes;
    cu_result =
        cuPointerGetAttribute(&attributes, CU_POINTER_ATTRIBUTE_MEMORY_TYPE,
                              reinterpret_cast<CUdeviceptr>(addr));

    if (cu_result != CUDA_SUCCESS)
    {
        fprintf(stderr, "cuPointerGetAttribute() error=%d\n", cu_result);
        return;
    }

    switch (attributes)
    {
    case CU_MEMORYTYPE_HOST:
        munmap(addr, size);
        break;
    case CU_MEMORYTYPE_DEVICE:
        cuMemFree((CUdeviceptr)addr);
        LOG(INFO) << "Pointer is located in GPU memory" << std::endl;
        break;
    default:
        assert(false);
        break;
    }
#else
    numa_free(addr, size);
#endif
}

volatile bool running = true;
std::atomic<size_t> total_batch_count(0);

int initiatorWorker(RdmaTransport *engine, SegmentID segment_id, int thread_id, void *addr)
{
    bindToSocket(thread_id % NR_SOCKETS);
    TransferRequest::OpCode opcode;
    if (FLAGS_operation == "read")
        opcode = TransferRequest::READ;
    else if (FLAGS_operation == "write")
        opcode = TransferRequest::WRITE;
    else
    {
        LOG(ERROR) << "Unsupported operation: must be 'read' or 'write'";
        exit(EXIT_FAILURE);
    }

    auto segment_desc = engine->meta()->getSegmentDescByID(segment_id);
    uint64_t remote_base = (uint64_t)segment_desc->buffers[thread_id % NR_SOCKETS].addr;

    size_t batch_count = 0;
    while (running)
    {
        // segment_desc = engine->meta()->getSegmentDescByID(segment_id, true);

        // if (segment_desc == nullptr || segment_desc->buffers.size() < 2)
        //     continue;
        // remote_base = (uint64_t)segment_desc->buffers[thread_id % NR_SOCKETS].addr;

        auto batch_id = engine->allocateBatchID(FLAGS_batch_size);
        int ret = 0;
        std::vector<TransferRequest> requests;
        for (int i = 0; i < FLAGS_batch_size; ++i)
        {
            TransferRequest entry;
            entry.opcode = opcode;
            entry.length = FLAGS_block_size;
            entry.source = (uint8_t *)(addr) + FLAGS_block_size * (i * FLAGS_threads + thread_id);
            entry.target_id = segment_id;
            entry.target_offset = remote_base + FLAGS_block_size * (i * FLAGS_threads + thread_id);
            requests.emplace_back(entry);
        }

        ret = engine->submitTransfer(batch_id, requests);
        LOG_ASSERT(!ret);
        for (int task_id = 0; task_id < FLAGS_batch_size; ++task_id)
        {
            bool completed = false;
            TransferStatus status;
            while (!completed)
            {
                int ret = engine->getTransferStatus(batch_id, task_id, status);
                LOG_ASSERT(!ret);
                if (status.s == TransferStatusEnum::COMPLETED)
                    completed = true;
                else if (status.s == TransferStatusEnum::FAILED)
                    completed = true;
            }
        }
        
        ret = engine->freeBatchID(batch_id);
        LOG_ASSERT(!ret);
        batch_count++;
    }
    LOG(INFO) << "Worker " << thread_id << " stopped!";
    total_batch_count.fetch_add(batch_count);
    return 0;
}

std::string loadNicPriorityMatrix()
{
    if (!FLAGS_nic_priority_matrix.empty())
    {
        std::ifstream file(FLAGS_nic_priority_matrix);
        if (file.is_open())
        {
            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
            file.close();
            return content;
        }
    }
    return "{\"cpu:0\": [[\"" + FLAGS_device_name + "\"], []], \"cpu:1\": [[\"" + FLAGS_device_name + "\"], []]}";
}

int initiator()
{
    auto metadata_client = std::make_shared<TransferMetadata>(FLAGS_metadata_server);
    LOG_ASSERT(metadata_client);

    auto nic_priority_matrix = loadNicPriorityMatrix();
    const size_t dram_buffer_size = 1ull << 30;
    auto engine = std::make_unique<TransferEngine>(metadata_client);

    void** args = (void**) malloc(2 * sizeof(void*));
    args[0] = (void*)nic_priority_matrix.c_str();
    args[1] = nullptr;

    const string& connectable_name = FLAGS_local_server_name;
    engine->init(FLAGS_local_server_name.c_str(), connectable_name.c_str(), 12345);
    RdmaTransport* xport = static_cast<RdmaTransport*>(engine->installOrGetTransport("rdma", args));

    LOG_ASSERT(engine);

    void *addr[NR_SOCKETS] = {nullptr};
    for (int i = 0; i < NR_SOCKETS; ++i)
    {
        addr[i] = allocateMemoryPool(dram_buffer_size, i);
        int rc = engine->registerLocalMemory(addr[i], dram_buffer_size, "cpu:" + std::to_string(i));
        LOG_ASSERT(!rc);
    }

    auto segment_id = engine->openSegment(FLAGS_segment_id.c_str());

    std::thread workers[FLAGS_threads];

    struct timeval start_tv, stop_tv;
    gettimeofday(&start_tv, nullptr);

    for (int i = 0; i < FLAGS_threads; ++i)
        workers[i] = std::thread(initiatorWorker, xport, segment_id, i, addr[i % NR_SOCKETS]);

    sleep(FLAGS_duration);
    running = false;

    for (int i = 0; i < FLAGS_threads; ++i)
        workers[i].join();

    gettimeofday(&stop_tv, nullptr);
    auto duration = (stop_tv.tv_sec - start_tv.tv_sec) + (stop_tv.tv_usec - start_tv.tv_usec) / 1000000.0;
    auto batch_count = total_batch_count.load();

    LOG(INFO) << "Test completed: duration "
              << std::fixed << std::setprecision(2)
              << duration
              << ", batch count "
              << batch_count
              << ", throughput "
              << (batch_count * FLAGS_batch_size * FLAGS_block_size) / duration / 1000000000.0;

    for (int i = 0; i < NR_SOCKETS; ++i)
    {
        engine->unregisterLocalMemory(addr[i]);
        freeMemoryPool(addr[i], dram_buffer_size);
    }

    engine->uninstallTransport("rdma");
    return 0;
}

int target()
{
    auto metadata_client = std::make_shared<TransferMetadata>(FLAGS_metadata_server);
    LOG_ASSERT(metadata_client);

    auto nic_priority_matrix = loadNicPriorityMatrix();

    const size_t dram_buffer_size = 1ull << 30;
    auto engine = std::make_unique<TransferEngine>(metadata_client);

    void** args = (void**) malloc(2 * sizeof(void*));
    args[0] = (void*)nic_priority_matrix.c_str();
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

    if (FLAGS_mode == "initiator")
        return initiator();
    else if (FLAGS_mode == "target")
        return target();

    LOG(ERROR) << "Unsupported mode: must be 'initiator' or 'target'";
    exit(EXIT_FAILURE);
}
