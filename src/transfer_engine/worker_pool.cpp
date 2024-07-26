// worker_pool.cpp
// Copyright (C) 2024 Feng Ren

#include "transfer_engine/transfer_engine.h"
#include "transfer_engine/worker_pool.h"
#include "transfer_engine/rdma_endpoint.h"

namespace mooncake
{
    WorkerPool::WorkerPool(RdmaContext &context, int numa_socket_id)
        : context_(context),
          numa_socket_id_(numa_socket_id),
          workers_running_(true),
          suspended_flag_(false),
          endpoint_set_version_(0)
    {
        worker_thread_.emplace_back(std::thread(std::bind(&WorkerPool::worker, this)));
    }

    WorkerPool::~WorkerPool()
    {
        if (workers_running_)
        {
            cond_var_.notify_all();
            workers_running_ = false;
            for (auto &entry : worker_thread_)
                entry.join();
        }
    }

    void WorkerPool::insertEndPoint(std::shared_ptr<RdmaEndPoint> &endpoint)
    {
        RWSpinlock::WriteGuard guard(endpoint_set_lock_);
        endpoint_set_.insert(endpoint);
        endpoint_set_version_++;
    }

    void WorkerPool::removeEndPoint(std::shared_ptr<RdmaEndPoint> &endpoint)
    {
        RWSpinlock::WriteGuard guard(endpoint_set_lock_);
        endpoint_set_.erase(endpoint);
        endpoint_set_version_++;
    }

    void WorkerPool::notify()
    {
        if (!suspended_flag_.load(std::memory_order_acquire))
            return;
        cond_var_.notify_all();
    }

    void WorkerPool::worker()
    {
        bindToSocket(numa_socket_id_);
        std::vector<std::shared_ptr<RdmaEndPoint>> endpoint_list;
        uint64_t version = 0;
        uint64_t post_slice_count = 0, ack_slice_count = 0;
        uint64_t last_suspend_clock = getCurrentTimeInNano();
        const static size_t kMinimalSuspendInterval = 100000000; // 100 ms

        while (workers_running_)
        {
            auto new_version = endpoint_set_version_.load(std::memory_order_relaxed);
            if (new_version != version)
            {
                RWSpinlock::ReadGuard guard(endpoint_set_lock_);
                endpoint_list.clear();
                for (auto &entry : endpoint_set_)
                    endpoint_list.push_back(entry);
                if (new_version != endpoint_set_version_.load(std::memory_order_relaxed))
                    continue;
                version = new_version;
            }

            post_slice_count = 0;
            for (auto &endpoint : endpoint_list)
                post_slice_count += endpoint->submittedSliceCount();

            if (post_slice_count == ack_slice_count)
            {
                uint64_t suspend_clock = getCurrentTimeInNano();
                if (suspend_clock - last_suspend_clock >= kMinimalSuspendInterval)
                {
                    // Wait until WorkerPool::notify()
                    std::unique_lock<std::mutex> lock(cond_mutex_);
                    suspended_flag_.store(true);
                    cond_var_.wait_for(lock, std::chrono::seconds(1));
                    suspended_flag_.store(false);
                    last_suspend_clock = suspend_clock;
                    continue;
                }
            }

            for (auto &endpoint : endpoint_list)
            {
                if (!endpoint->connected())
                {
                    if (endpoint->setupConnectionsByActive())
                    {
                        LOG(ERROR) << "Worker: Failed to connect peer endpoints";
                        endpoint->disconnect();
                    }
                }
                if (endpoint->performPostSend())
                    LOG(ERROR) << "Worker: Failed to send work requests";
            }

            for (int cq_index = 0; cq_index < context_.cqCount(); ++cq_index)
            {
                ibv_wc wc[16];
                int nr_poll = context_.poll(16, wc, cq_index);
                if (nr_poll < 0)
                {
                    LOG(ERROR) << "Worker: Failed to poll completion queues";
                    continue;
                }
                ack_slice_count += nr_poll;
                for (int i = 0; i < nr_poll; ++i)
                {
                    TransferEngine::Slice *slice = (TransferEngine::Slice *)wc[i].wr_id;
                    __sync_fetch_and_sub(slice->rdma.qp_depth, 1);
                    if (wc[i].status != IBV_WC_SUCCESS)
                    {
                        LOG(ERROR) << "Worker: Process failed for slice (opcode: " << slice->opcode
                                   << ", source_addr: " << slice->source_addr
                                   << ", length: " << slice->length
                                   << ", dest_addr: " << slice->rdma.dest_addr
                                   << "): " << ibv_wc_status_str(wc[i].status);
                        slice->status = TransferEngine::Slice::FAILED;
                        __sync_fetch_and_add(&slice->task->failed_slice_count, 1);
                    }
                    else
                    {
                        slice->status = TransferEngine::Slice::SUCCESS;
                        __sync_fetch_and_add(&slice->task->transferred_bytes, slice->length);
                        __sync_fetch_and_add(&slice->task->success_slice_count, 1);
                    }
                }
            }
        }
    }
}