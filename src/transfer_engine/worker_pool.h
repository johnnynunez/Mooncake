// worker_pool.h
// Copyright (C) 2024 Feng Ren

#ifndef WORKER_H
#define WORKER_H

#include <unordered_set>
#include <queue>

#include "transfer_engine/rdma_context.h"

namespace mooncake
{
    class WorkerPool
    {
    public:
        WorkerPool(RdmaContext &context, int numa_socket_id = 0);

        ~WorkerPool();

        void insertEndPoint(std::shared_ptr<RdmaEndPoint> &endpoint);

        void removeEndPoint(std::shared_ptr<RdmaEndPoint> &endpoint);

        // 由 TransferEngine 调用，向队列添加 Slice
        int submitPostSend(const std::vector<TransferEngine::Slice *> &slice_list);

    private:
        void performPostSend();

        void performPollCq();

        void processFailedSlice(TransferEngine::Slice *slice);

        void worker();

    private:
        RdmaContext &context_;
        const int numa_socket_id_;
        std::vector<std::thread> worker_thread_;
        std::atomic<bool> workers_running_;
        std::mutex cond_mutex_;
        std::condition_variable cond_var_;
        std::atomic<bool> suspended_flag_;

        RWSpinlock slice_list_lock_;
        std::unordered_map<std::string, std::vector<TransferEngine::Slice *>> slice_list_map_;
        std::atomic<uint64_t> submitted_slice_count_, processed_slice_count_;
    };
}

#endif // WORKER_H
