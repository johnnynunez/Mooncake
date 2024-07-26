// worker_pool.h
// Copyright (C) 2024 Feng Ren

#ifndef WORKER_H
#define WORKER_H

#include <unordered_set>

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

        void notify();

    private:
        void worker();

    private:
        RdmaContext &context_;
        const int numa_socket_id_;
        std::vector<std::thread> worker_thread_;
        std::atomic<bool> workers_running_;
        std::mutex cond_mutex_;
        std::condition_variable cond_var_;
        std::atomic<bool> suspended_flag_;

        RWSpinlock endpoint_set_lock_;
        std::unordered_set<std::shared_ptr<RdmaEndPoint>> endpoint_set_;
        std::atomic<uint64_t> endpoint_set_version_;
    };
}

#endif // WORKER_H
