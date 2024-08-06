// worker_pool.cpp
// Copyright (C) 2024 Feng Ren

#include <sys/epoll.h>

#include "transfer_engine/rdma_context.h"
#include "transfer_engine/rdma_endpoint.h"
#include "transfer_engine/transfer_engine.h"
#include "transfer_engine/worker_pool.h"

namespace mooncake
{
    WorkerPool::WorkerPool(RdmaContext &context, int numa_socket_id)
        : context_(context),
          numa_socket_id_(numa_socket_id),
          workers_running_(true),
          suspended_flag_(false),
          submitted_slice_count_(0),
          processed_slice_count_(0),
          slice_list_map_size_(0)
    {
        worker_thread_.emplace_back(std::thread(std::bind(&WorkerPool::transferWorker, this)));
        worker_thread_.emplace_back(std::thread(std::bind(&WorkerPool::monitorWorker, this)));
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

    int WorkerPool::submitPostSend(const std::vector<TransferEngine::Slice *> &slice_list)
    {
        std::unordered_multimap<std::string, TransferEngine::Slice *> slice_list_map;
        for (auto &slice : slice_list)
        {
            auto &peer_segment_desc = slice->peer_segment_desc;
            int buffer_id, device_id;
            if (TransferEngine::selectDevice(peer_segment_desc, slice->rdma.dest_addr, buffer_id, device_id))
            {
                LOG(ERROR) << "Unrecorgnized target address " << slice->rdma.dest_addr << " on " << peer_segment_desc->name;
                return -1;
            }
            slice->rdma.dest_rkey = peer_segment_desc->buffers[buffer_id].rkey[device_id];
            auto peer_nic_path = MakeNicPath(peer_segment_desc->name, peer_segment_desc->devices[device_id].name);
            slice_list_map.emplace(std::make_pair(peer_nic_path, slice));
        }
        RWSpinlock::WriteGuard guard(slice_list_lock_);
        for (auto &entry : slice_list_map)
        {
            slice_list_map_[entry.first].push_back(entry.second);
        }
        submitted_slice_count_ += slice_list.size();
        slice_list_map_size_.fetch_add(slice_list.size(), std::memory_order_relaxed);
        if (suspended_flag_.load(std::memory_order_relaxed))
        {
            cond_var_.notify_all();
        }
        return 0;
    }

    void WorkerPool::performPostSend()
    {
        if (slice_list_map_size_.load(std::memory_order_relaxed) == 0)
            return;
        RWSpinlock::WriteGuard guard(slice_list_lock_);
        uint64_t slice_list_map_size = 0;
        for (auto &entry : slice_list_map_)
        {
            auto endpoint = context_.endpoint(entry.first);
            if (!endpoint)
            {
                LOG(ERROR) << "Cannot allocate endpoint: " << entry.first;
                for (auto &slice : entry.second)
                    processFailedSlice(slice);
                entry.second.clear();
                continue;
            }
            if (!endpoint->connected() && endpoint->setupConnectionsByActive())
            {
                LOG(ERROR) << "Cannot make connection for endpoint: " << entry.first;
                for (auto &slice : entry.second)
                    processFailedSlice(slice);
                entry.second.clear();
                continue;
            }
#ifdef USE_FAKE_POST_SEND
            for (auto &slice : entry.second)
            {
                slice->status = TransferEngine::Slice::SUCCESS;
                __sync_fetch_and_add(&slice->task->transferred_bytes, slice->length);
                __sync_fetch_and_add(&slice->task->success_slice_count, 1);
                processed_slice_count_++;
            }
            entry.second.clear();
#else
            std::vector<TransferEngine::Slice *> failed_slice_list;
            if (endpoint->submitPostSend(entry.second, failed_slice_list))
            {
                for (auto &slice : failed_slice_list)
                    processFailedSlice(slice);
            }
            slice_list_map_size += entry.second.size();
#endif
        }
        slice_list_map_size_.store(slice_list_map_size, std::memory_order_relaxed);
    }

    void WorkerPool::performPollCq()
    {
        for (int cq_index = 0; cq_index < context_.cqCount(); ++cq_index)
        {
            ibv_wc wc[16];
            int nr_poll = context_.poll(16, wc, cq_index);
            if (nr_poll < 0)
            {
                LOG(ERROR) << "Worker: Failed to poll completion queues";
                continue;
            }

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
                    RWSpinlock::WriteGuard guard(slice_list_lock_);
                    processFailedSlice(slice);
                    slice_list_map_size_.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    slice->status = TransferEngine::Slice::SUCCESS;
                    __sync_fetch_and_add(&slice->task->transferred_bytes, slice->length);
                    __sync_fetch_and_add(&slice->task->success_slice_count, 1);
                    processed_slice_count_++;
                }
            }
        }
    }

    void WorkerPool::processFailedSlice(TransferEngine::Slice *slice)
    {
        if (slice->rdma.retry_cnt == slice->rdma.max_retry_cnt)
        {
            slice->status = TransferEngine::Slice::FAILED;
            __sync_fetch_and_add(&slice->task->failed_slice_count, 1);
            processed_slice_count_++;
        }
        else
        {
            slice->rdma.retry_cnt++;
            auto &peer_segment_desc = slice->peer_segment_desc;
            int buffer_id, device_id;
            if (TransferEngine::selectDevice(peer_segment_desc, slice->rdma.dest_addr, buffer_id, device_id, slice->rdma.retry_cnt))
            {
                slice->status = TransferEngine::Slice::FAILED;
                __sync_fetch_and_add(&slice->task->failed_slice_count, 1);
                processed_slice_count_++;
                return;
            }
            slice->rdma.dest_rkey = peer_segment_desc->buffers[buffer_id].rkey[device_id];
            auto peer_nic_path = MakeNicPath(peer_segment_desc->name, peer_segment_desc->devices[device_id].name);
            slice_list_map_[peer_nic_path].push_back(slice);
        }
    }

    void WorkerPool::transferWorker()
    {
        bindToSocket(numa_socket_id_);
        const static uint64_t kWaitPeriodInNano = 100000000; // 100ms
        uint64_t last_wait_ts = getCurrentTimeInNano();
        while (workers_running_)
        {
            if (processed_slice_count_ == submitted_slice_count_)
            {
                uint64_t curr_wait_ts = getCurrentTimeInNano();
                if (curr_wait_ts - last_wait_ts > kWaitPeriodInNano)
                {
                    std::unique_lock<std::mutex> lock(cond_mutex_);
                    suspended_flag_ = true;
                    cond_var_.wait_for(lock, std::chrono::seconds(1));
                    suspended_flag_ = false;
                    last_wait_ts = curr_wait_ts;
                }
            }
            performPostSend();
#ifndef USE_FAKE_POST_SEND
            performPollCq();
#endif
        }
    }

    int WorkerPool::doProcessContextEvents()
    {
        ibv_async_event event;
        if (ibv_get_async_event(context_.context(), &event) < 0)
            return -1;
        LOG(INFO) << "Received context async event: " << ibv_event_type_str(event.event_type);
        // TODO 处理策略？
        // - 鸵鸟（只打印警告？）
        // - 杀掉进程？
        // - 完全删除该 Context 并重新创建？
        ibv_ack_async_event(&event);
        return 0;
    }

    void WorkerPool::monitorWorker()
    {
        bindToSocket(numa_socket_id_);
        while (workers_running_)
        {
            struct epoll_event event;
            int num_events = epoll_wait(context_.eventFd(), &event, 1, 100);
            if (num_events < 0)
            {
                PLOG(ERROR) << "Failed to call epoll wait";
                continue;
            }

            if (num_events == 0)
                continue;

            LOG(ERROR) << "Received event, fd: " << event.data.fd
                       << ", events: " << event.events;

            if (!(event.events & EPOLLIN))
                continue;

            if (event.data.fd == context_.context()->async_fd)
                doProcessContextEvents();
        }
    }
}