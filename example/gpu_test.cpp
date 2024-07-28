#include "transfer_engine/transfer_engine.h"

#include <cassert>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <iomanip>
#include <sys/time.h>

#ifdef USE_CUDA
#include "cuda.h"

static CUcontext cuContext;

#define CUCHECK(stmt)                   \
    do                                  \
    {                                   \
        CUresult result = (stmt);       \
        assert(CUDA_SUCCESS == result); \
    } while (0)
#endif

DEFINE_string(metadata_server, "optane14:12345", "etcd server host address");
DEFINE_string(mode, "initiator",
              "Running mode: initiator or target. Initiator node read/write "
              "data blocks from target node");
DEFINE_string(operation, "write", "Operation type: read or write");
// {
//     "cpu:0": [["mlx5_2"], ["mlx5_3"]],
//     "cpu:1": [["mlx5_3"], ["mlx5_2"]],
//     "cuda:0": [["mlx5_2"], ["mlx5_3"]],
// }
DEFINE_string(nic_priority_matrix, "{\"cpu:0\": [[\"mlx5_0\"], [\"mlx5_0\"]], \"cpu:1\": [[\"mlx5_0\"], [\"mlx5_0\"]]}", "NIC priority matrix");
DEFINE_string(segment_id, "optane14", "Segment ID to access data");
DEFINE_int32(batch_size, 128, "Batch size");
DEFINE_int32(block_size, 4096, "Block size for each transfer request");
DEFINE_int32(duration, 10, "Test duration in seconds");
DEFINE_int32(threads, 4, "Task submission threads");

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

static void *init_gpu_and_allocate_memory(size_t size)
{
#ifdef USE_CUDA
    CUresult cu_result = cuInit(0);
    printf("initializing CUDA\n");
    if (cu_result != CUDA_SUCCESS)
    {
        fprintf(stderr, "cuInit(0) returned %d\n", cu_result);
        return NULL;
    }

    int dev_id = 0; // TODO: get dev id

    CUdevice cu_dev;
    CUCHECK(cuDeviceGet(&cu_dev, dev_id));
    /* Create context */
    cu_result = cuCtxCreate(&cuContext, CU_CTX_MAP_HOST, cu_dev);
    if (cu_result != CUDA_SUCCESS)
    {
        fprintf(stderr, "cuCtxCreate() error=%d\n", cu_result);
        return NULL;
    }

    cu_result = cuCtxSetCurrent(cuContext);
    if (cu_result != CUDA_SUCCESS)
    {
        fprintf(stderr, "cuCtxSetCurrent() error=%d\n", cu_result);
        return NULL;
    }

    CUdeviceptr d_A;
    cu_result = cuMemAlloc(&d_A, size);
    if (cu_result != CUDA_SUCCESS)
    {
        fprintf(stderr, "cuMemAlloc error=%d\n", cu_result);
        return NULL;
    }

    return ((void *)d_A);
#else
    return NULL;
#endif
}

static void *allocateMemoryPool(size_t size, int socket_id)
{
    return numa_alloc_onnode(size, socket_id);
    // void *start_addr;
    // start_addr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
    //                   MAP_ANON | MAP_PRIVATE,
    //                   -1, 0);
    // if (start_addr == MAP_FAILED)
    // {
    //     PLOG(ERROR) << "Failed to allocate memory";
    //     return nullptr;
    // }
    // return start_addr;
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
    case CU_POINTER_ATTRIBUTE_HOST_POINTER:
        numa_free(addr, size);
        break;
    case CU_POINTER_ATTRIBUTE_DEVICE_POINTER:
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

int initiatorWorker(TransferEngine *engine, SegmentID segment_id, int thread_id,
                    void *addr)
{
    bindToSocket(0);
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

    auto segment_desc = engine->getSegmentDescByID(segment_id);
    uint64_t remote_base = (uint64_t)segment_desc->buffers[0].addr;

    size_t batch_count = 0;
    while (running)
    {
        auto batch_id = engine->allocateBatchID(FLAGS_batch_size);
        LOG_ASSERT(batch_id >= 0);
        int ret = 0;
        std::vector<TransferRequest> requests;
        for (int i = 0; i < FLAGS_batch_size; ++i)
        {
            TransferRequest entry;
            entry.opcode = opcode;
            entry.length = FLAGS_block_size;
            entry.source = (uint8_t *)(addr) +
                           FLAGS_block_size * (i * FLAGS_threads + thread_id);
            entry.target_id = segment_id;
            entry.target_offset =
                remote_base + FLAGS_block_size * (i * FLAGS_threads + thread_id);
            requests.emplace_back(entry);
        }

        ret = engine->submitTransfer(batch_id, requests);
        LOG_ASSERT(!ret);
        while (true)
        {
            std::vector<TransferStatus> status;
            ret = engine->getTransferStatus(batch_id, status);
            LOG_ASSERT(!ret);
            int completed = 0, failed = 0;
            for (int i = 0; i < FLAGS_batch_size; ++i)
                if (status[i].s == TransferStatusEnum::COMPLETED)
                    completed++;
                else if (status[i].s == TransferStatusEnum::FAILED)
                    failed++;
            if (completed + failed == FLAGS_batch_size)
            {
                if (failed)
                    LOG(WARNING) << "Found " << failed << " failures in this batch";
                break;
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

int initiator()
{
    auto metadata_client =
        std::make_unique<TransferMetadata>(FLAGS_metadata_server);
    LOG_ASSERT(metadata_client);

    const size_t dram_buffer_size = 1ull << 30;
    auto engine = std::make_unique<TransferEngine>(metadata_client, getHostname(),
                                                   FLAGS_nic_priority_matrix);
    LOG_ASSERT(engine);

    void *addr = allocateMemoryPool(dram_buffer_size, 0);
    engine->registerLocalMemory(addr, dram_buffer_size, "cpu:0");

    auto segment_id = engine->getSegmentID(FLAGS_segment_id);
    LOG_ASSERT(segment_id >= 0);

    std::thread workers[FLAGS_threads];

    struct timeval start_tv, stop_tv;
    gettimeofday(&start_tv, nullptr);

    for (int i = 0; i < FLAGS_threads; ++i)
        workers[i] =
            std::thread(initiatorWorker, engine.get(), segment_id, i, addr);

    sleep(FLAGS_duration);
    running = false;

    for (int i = 0; i < FLAGS_threads; ++i)
        workers[i].join();

    gettimeofday(&stop_tv, nullptr);
    auto duration = (stop_tv.tv_sec - start_tv.tv_sec) +
                    (stop_tv.tv_usec - start_tv.tv_usec) / 1000000.0;
    auto batch_count = total_batch_count.load();

    LOG(INFO) << "Test completed: duration " << std::fixed << std::setprecision(2)
              << duration << ", batch count " << batch_count << ", throughput "
              << (batch_count * FLAGS_batch_size * FLAGS_block_size) / duration /
                     1000000000.0;

    engine->unregisterLocalMemory(addr);
    freeMemoryPool(addr, dram_buffer_size);

    return 0;
}

int target()
{
    auto metadata_client =
        std::make_unique<TransferMetadata>(FLAGS_metadata_server);
    LOG_ASSERT(metadata_client);

    const size_t vram_buffer_size = 1ull << 30;
    auto engine = std::make_unique<TransferEngine>(metadata_client, getHostname(),
                                                   FLAGS_nic_priority_matrix);
    LOG_ASSERT(engine);

    void *addr = init_gpu_and_allocate_memory(vram_buffer_size);
    engine->registerLocalMemory(addr, vram_buffer_size, "cpu:0");

    while (true)
        sleep(1);

    engine->unregisterLocalMemory(addr);
    freeMemoryPool(addr, vram_buffer_size);

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