// worker_pool.h
// Copyright (C) 2024 Feng Ren

#ifndef WORKER_H
#define WORKER_H

#include <queue>
#include <unordered_set>

#include "transfer_engine/rdma_context.h"

namespace mooncake
{
    class WorkerPool
    {
    public:
        WorkerPool(RdmaContext &context, int numa_socket_id = 0);

        ~WorkerPool();

        // 由 TransferEngine 调用，向队列添加 Slice
        int submitPostSend(const std::vector<TransferEngine::Slice *> &slice_list);

    private:
        void performPostSend(int shard_id);

        void performPollCq(int thread_id);

        void processFailedSlice(TransferEngine::Slice *slice);

        void transferWorker(int thread_id);

        void monitorWorker();

        int doProcessContextEvents();

    private:
        RdmaContext &context_;
        const int numa_socket_id_;

        std::vector<std::thread> worker_thread_;
        std::atomic<bool> workers_running_;
        std::atomic<int> suspended_flag_;

        std::mutex cond_mutex_;
        std::condition_variable cond_var_;

        const static int kShardCount = 8;
        RWSpinlock slice_list_lock_[kShardCount];

        using SliceList = std::vector<TransferEngine::Slice *>;
        std::unordered_map<std::string, SliceList> slice_list_map_[kShardCount];
        std::atomic<uint64_t> submitted_slice_count_, processed_slice_count_;
        std::atomic<uint64_t> slice_list_size_[kShardCount];
    };
}

#endif // WORKER_H
