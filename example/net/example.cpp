#include "net/transfer_engine.h"

#include <iomanip>
#include <sys/time.h>
#include <glog/logging.h>
#include <gflags/gflags.h>

DEFINE_string(metadata_server, "optane21:12345", "etcd server host address");
DEFINE_string(mode, "initiator", "Running mode: initiator or target. Initiator node read/write data blocks from target node");
DEFINE_string(operation, "read", "Operation type: read or write");
// {
//     "cpu:0": [["mlx5_2"], ["mlx5_3"]],
//     "cpu:1": [["mlx5_3"], ["mlx5_2"]],
//     "cuda:0": [["mlx5_2"], ["mlx5_3"]],
// }
DEFINE_string(nic_priority_matrix, "{\"cpu:0\": [[\"mlx5_2\"], [\"mlx5_3\"]], \"cpu:1\": [[\"mlx5_3\"], [\"mlx5_2\"]]}", "NIC priority matrix");
DEFINE_string(segment_id, "optane20/cpu:0", "Segment ID to access data");
DEFINE_int32(batch_size, 128, "Batch size");
DEFINE_int32(block_size, 4096, "Block size for each transfer request");
DEFINE_int32(duration, 10, "Test duration in seconds");
DEFINE_int32(threads, 4, "Task submission threads");

static std::string get_hostname()
{
    char hostname[256];
    if (gethostname(hostname, 256))
    {
        PLOG(ERROR) << "Failed to get hostname";
        return "";
    }
    return hostname;
}

volatile bool running = true;
std::atomic<size_t> total_batch_count(0);

int initiator_worker(TransferEngine *engine, SegmentID segment_id, int thread_id)
{
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

    size_t batch_count = 0;
    while (running)
    {
        auto batch_id = engine->allocate_batch_id(FLAGS_batch_size);
        LOG_ASSERT(batch_id >= 0);
        int ret = 0;
        std::vector<TransferRequest> requests;
        for (int i = 0; i < FLAGS_batch_size; ++i)
        {
            TransferRequest entry;
            entry.opcode = opcode;
            entry.length = FLAGS_block_size;
            entry.source = (uint8_t *)engine->get_dram_buffer() + FLAGS_block_size * (i * FLAGS_threads + thread_id);
            entry.target_id = segment_id;
            entry.target_offset = FLAGS_block_size * (i * FLAGS_threads + thread_id);
            requests.emplace_back(entry);
        }

        ret = engine->submit_transfer(batch_id, requests);
        LOG_ASSERT(!ret);
        while (true)
        {
            std::vector<TransferStatus> status;
            ret = engine->get_transfer_status(batch_id, status);
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

        ret = engine->free_batch_id(batch_id);
        LOG_ASSERT(!ret);
        batch_count++;
    }
    LOG(INFO) << "Worker " << thread_id << " stopped!";
    total_batch_count.fetch_add(batch_count);
    return 0;
}

int initiator()
{
    auto metadata_client = std::make_unique<TransferMetadata>(FLAGS_metadata_server);
    LOG_ASSERT(metadata_client);

    const size_t dram_buffer_size = 1ull << 30;
    auto engine = std::make_unique<TransferEngine>(metadata_client,
                                                   get_hostname(),
                                                   dram_buffer_size,
                                                   0,
                                                   FLAGS_nic_priority_matrix);
    LOG_ASSERT(engine);
    engine->update_rnic_prob({200, 100});

    auto segment_id = engine->get_segment_id(FLAGS_segment_id);
    LOG_ASSERT(segment_id >= 0);

    std::thread workers[FLAGS_threads];

    struct timeval start_tv, stop_tv;
    gettimeofday(&start_tv, nullptr);

    for (int i = 0; i < FLAGS_threads; ++i)
        workers[i] = std::thread(initiator_worker, engine.get(), segment_id, i);

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

    return 0;
}

int target()
{
    auto metadata_client = std::make_unique<TransferMetadata>(FLAGS_metadata_server);
    LOG_ASSERT(metadata_client);

    const size_t dram_buffer_size = 1ull << 30;
    auto engine = std::make_unique<TransferEngine>(metadata_client,
                                                   get_hostname(),
                                                   dram_buffer_size,
                                                   0,
                                                   FLAGS_nic_priority_matrix);
    LOG_ASSERT(engine);
    engine->update_rnic_prob({200, 100});

    while (true)
        sleep(1);

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
